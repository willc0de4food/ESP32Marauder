#include "Buffer.h"
#include "lang_var.h"
#include "GpsInterface.h"

#ifdef HAS_GPS
extern GpsInterface gps_obj;
#endif

// PPI (Per-Packet Information) constants — see CACE/Riverbed spec and Wireshark PPI dissector.
// We emit a PPI fixed header (8 bytes) followed by a single PPI Geolocation field
// (type 30002) with lat/lon/alt sub-fields, then a small radiotap header carrying
// per-frame RSSI + channel, then the 802.11 frame as the encapsulated payload.
// DLT switches from 105 (raw 802.11) to 192 (PPI) in the pcap global header.
// The PPI's inner pph_dlt is 127 (radiotap), since radiotap now wraps the 802.11.
#define PPI_DLT                  192
#define PPI_INNER_DLT_RADIOTAP   127
#define PPI_FIELD_TYPE_GEOLOC    30002
// Geolocation "present" bitmask flags (bit positions per the PPI Geolocation spec):
#define PPI_GEO_PRESENT_LAT      (1u << 1)
#define PPI_GEO_PRESENT_LON      (1u << 2)
#define PPI_GEO_PRESENT_ALT      (1u << 3)
#define PPI_GEO_VERSION          2  // current Geolocation spec version

// Radiotap (inside PPI). Minimal radiotap header carrying channel + antenna signal:
//   Offset 0:    it_version (1 byte) = 0
//   Offset 1:    it_pad     (1 byte) = 0
//   Offset 2-3:  it_len     (2 bytes LE) = 13
//   Offset 4-7:  it_present (4 bytes LE) = bits 3 (Channel) + 5 (Antenna Signal) = 0x28
//   Offset 8-9:  channel freq MHz (2 bytes LE)
//   Offset 10-11:channel flags    (2 bytes LE) — 0x00a0 = 2 GHz + CCK
//   Offset 12:   antenna signal   (1 byte signed dBm)
#define RADIOTAP_PRESENT_FIELDS  ((1u << 3) | (1u << 5))  // Channel + Antenna Signal
#define RADIOTAP_HEADER_LEN      13
#define RADIOTAP_CH_FLAGS_2GHZ   0x00a0  // 2 GHz spectrum + CCK (b/g compatible)

// Sizes:
//   PPI fixed header              = 8 bytes  (ver + flags + len + dlt)
//   PPI field type/len header     = 4 bytes  (pfh_type + pfh_datalen)
//   Geolocation sub-header        = 8 bytes  (geotag_ver + pad + len + present)
//   Lat/Lon/Alt sub-fields        = 12 bytes (3 x uint32 fixed-point)
//   ----------------------------- PPI header so far = 32 bytes ----------
//   Radiotap header               = 13 bytes (version + pad + len + present + ch + signal)
//   ----------------------------- per-frame overhead = 45 bytes ---------
//
// PPI's ph_len field covers ONLY the PPI fixed header + PPI fields (32 bytes).
// Radiotap is the start of the frame data (per inner DLT = 127), not a PPI
// field — so it must NOT be counted in ph_len. Wireshark uses ph_len to find
// where the inner-DLT frame begins; if it's too large, radiotap gets swallowed
// into the PPI "Reserved" tail and the 802.11 frame fails to dissect.
#define PPI_HEADER_LEN           32  // value written into the ph_len field
#define PPI_FRAME_OVERHEAD       45  // total per-frame bytes added (PPI + radiotap)

Buffer::Buffer(){
  bufA = (uint8_t*)malloc(BUF_SIZE);
  bufB = (uint8_t*)malloc(BUF_SIZE);
}

void Buffer::createFile(String name, bool is_pcap, bool is_gpx){
  int i=0;
  if (is_pcap) {
    do{
      fileName = "/"+name+"_"+(String)i+".pcap";
      i++;
    } while(fs->exists(fileName));
  }
  else if ((!is_pcap) && (!is_gpx)) {
    do{
      fileName = "/"+name+"_"+(String)i+".log";
      i++;
    } while(fs->exists(fileName));
  }
  else {
    do{
      fileName = "/"+name+"_"+(String)i+".gpx";
      i++;
    } while(fs->exists(fileName));
  }

  Serial.println(fileName);
  
  file = fs->open(fileName, FILE_WRITE);
  file.close();
}

void Buffer::open(bool is_pcap){
  bufSizeA = 0;
  bufSizeB = 0;

  bufSizeB = 0;

  writing = true;

  if (is_pcap) {
    // PPI captures need extra room in SNAP_LEN for the per-frame PPI header
    // (32 bytes — see PPI_FRAME_OVERHEAD above).
    uint32_t snap = is_ppi ? (SNAP_LEN + PPI_FRAME_OVERHEAD) : SNAP_LEN;
    uint32_t dlt  = is_ppi ? PPI_DLT : 105;
    write(uint32_t(0xa1b2c3d4)); // magic number
    write(uint16_t(2)); // major version number
    write(uint16_t(4)); // minor version number
    write(int32_t(0)); // GMT to local correction
    write(uint32_t(0)); // accuracy of timestamps
    write(snap); // max length of captured packets, in octets
    write(dlt); // data link type
  }
}

String Buffer::getFileName() {
  return this->fileName;
}

void Buffer::openFile(String file_name, fs::FS* fs, bool serial, bool is_pcap, bool is_gpx) {
  bool save_pcap = settings_obj.loadSetting<bool>("SavePCAP");
  if (!save_pcap) {
    this->fs = NULL;
    this->serial = false;
    writing = false;
    return;
  }
  this->fs = fs;
  this->serial = serial;
  if (this->fs) {
    createFile(file_name, is_pcap, is_gpx);
  }
  if (this->fs || this->serial) {
    open(is_pcap);
  } else {
    writing = false;
  }
}

void Buffer::pcapOpen(String file_name, fs::FS* fs, bool serial) {
  is_ppi = false;
  openFile(file_name, fs, serial, true);
}

void Buffer::pcapOpenPPI(String file_name, fs::FS* fs, bool serial) {
  is_ppi = true;
  openFile(file_name, fs, serial, true);
}

// Build and write the PPI fixed header + a Geolocation field with the current
// cached lat/lon/alt from gps_obj. Called once per packet (right after the
// pcap per-frame timestamp header, right before the 802.11 payload).
//
// PPI fixed-point encoding (per the PPI Geolocation spec):
//   lat = (degrees + 180.0) * 1e7   (uint32, LE)
//   lon = (degrees + 180.0) * 1e7
//   alt = (meters  + 180000.0) * 1e4   (fixed6_4 format — wireshark's `ppi_gps.alt`)
// We always emit lat/lon/alt fields. If there's no GPS fix, we emit zeros for
// lat/lon (= -180 degrees, an unambiguous sentinel that post-processors can
// filter on) so the pcap still parses cleanly.
void Buffer::writePpiHeader(uint32_t /*frame_len*/, int8_t rssi_dbm, uint8_t channel) {
  // --- PPI fixed header (8 bytes) ---
  write(uint8_t(0));                            // pph_version = 0
  write(uint8_t(0));                            // pph_flags   = 0 (32-bit aligned)
  write(uint16_t(PPI_HEADER_LEN));              // pph_len     = 32 (PPI header only — radiotap lives in the frame, not in PPI)
  write(uint32_t(PPI_INNER_DLT_RADIOTAP));      // pph_dlt     = 127 (encapsulated frame is radiotap → 802.11)

  // --- PPI Geolocation field header (4 bytes) ---
  write(uint16_t(PPI_FIELD_TYPE_GEOLOC));       // pfh_type    = 30002
  write(uint16_t(20));                          // pfh_datalen = 20 (sub-header 8 + lat/lon/alt 12)

  // --- Geolocation sub-header (8 bytes) ---
  write(uint8_t(PPI_GEO_VERSION));              // geotag_ver
  write(uint8_t(0));                            // geotag_pad
  write(uint16_t(20));                          // geotag_len (this sub-tag including its own header)
  write(uint32_t(PPI_GEO_PRESENT_LAT |
                 PPI_GEO_PRESENT_LON |
                 PPI_GEO_PRESENT_ALT));         // present bitmask

  // --- Lat / Lon / Alt fixed-point fields (12 bytes) ---
  uint32_t lat_fixed = 0;
  uint32_t lon_fixed = 0;
  uint32_t alt_fixed = 0;

#ifdef HAS_GPS
  if (gps_obj.getGpsModuleStatus() && gps_obj.getFixStatus()) {
    // gps_obj caches lat/lon as int32 in millionths of a degree (1e6 scale)
    // — the PPI fixed-point format wants 1e7 scale and a +180 degree offset.
    double lat_deg = (double)gps_obj.getLatInt() / 1000000.0;
    double lon_deg = (double)gps_obj.getLonInt() / 1000000.0;
    double alt_m   = (double)gps_obj.getAlt();
    lat_fixed = (uint32_t)((lat_deg + 180.0) * 1e7);
    lon_fixed = (uint32_t)((lon_deg + 180.0) * 1e7);
    alt_fixed = (uint32_t)((alt_m + 180000.0) * 1e4);
  }
#endif
  write(lat_fixed);
  write(lon_fixed);
  write(alt_fixed);

  // --- Radiotap header (13 bytes) — carries per-frame RSSI + channel ---
  // The PPI's pph_dlt above says "127 = radiotap follows", so any parser
  // (Wireshark, tcpdump, hcxdumptool, scapy) will pick up radiotap next.
  // Channel freq: 2412 MHz on ch1, +5 MHz per channel through ch13.
  uint16_t freq_mhz = (channel >= 1 && channel <= 14)
                    ? (uint16_t)(2412 + (channel - 1) * 5)
                    : 0;
  write(uint8_t(0));                            // it_version = 0
  write(uint8_t(0));                            // it_pad
  write(uint16_t(RADIOTAP_HEADER_LEN));         // it_len = 13
  write(uint32_t(RADIOTAP_PRESENT_FIELDS));     // it_present = ch + signal
  write(freq_mhz);                              // channel frequency (MHz)
  write(uint16_t(RADIOTAP_CH_FLAGS_2GHZ));      // channel flags
  write(uint8_t((uint8_t)rssi_dbm));            // antenna signal (signed dBm)
}

void Buffer::logOpen(String file_name, fs::FS* fs, bool serial) {
  openFile(file_name, fs, serial, false);
}

void Buffer::gpxOpen(String file_name, fs::FS* fs, bool serial) {
  openFile(file_name, fs, serial, false, true);
}

void Buffer::add(const uint8_t* buf, uint32_t len, bool is_pcap,
                 int8_t rssi_dbm, uint8_t channel){
  // PPI mode adds a fixed PPI_FRAME_OVERHEAD-byte header before each frame —
  // account for it in the buffer-full and double-buffer-flip checks so we don't
  // truncate frames mid-header.
  uint32_t total_len = (is_pcap && is_ppi) ? (len + PPI_FRAME_OVERHEAD) : len;

  // buffer is full -> drop packet
  if((useA && bufSizeA + total_len >= BUF_SIZE && bufSizeB > 0) || (!useA && bufSizeB + total_len >= BUF_SIZE && bufSizeA > 0)){
    //Serial.print(";");
    return;
  }

  if(useA && bufSizeA + total_len + 16 >= BUF_SIZE && bufSizeB == 0){
    useA = false;
    //Serial.println("\nswitched to buffer B");
  }
  else if(!useA && bufSizeB + total_len + 16 >= BUF_SIZE && bufSizeA == 0){
    useA = true;
    //Serial.println("\nswitched to buffer A");
  }

  uint32_t microSeconds = micros(); // e.g. 45200400 => 45s 200ms 400us
  uint32_t seconds = (microSeconds/1000)/1000; // e.g. 45200400/1000/1000 = 45200 / 1000 = 45s

  microSeconds -= seconds*1000*1000; // e.g. 45200400 - 45*1000*1000 = 45200400 - 45000000 = 400us (because we only need the offset)

  if (is_pcap) {
    write(seconds); // ts_sec
    write(microSeconds); // ts_usec
    write(total_len); // incl_len (frame + optional PPI header)
    write(total_len); // orig_len
    if (is_ppi) {
      writePpiHeader(len, rssi_dbm, channel);
    }
  }

  write(buf, len); // packet payload (always the original 802.11 frame)
}

void Buffer::append(wifi_promiscuous_pkt_t *packet, int len) {
  bool save_packet = settings_obj.loadSetting<bool>(text_table4[7]);
  if (save_packet) {
    // Pass through the per-frame RSSI and channel from the ESP32 promisc
    // RX control block — these populate the radiotap header in PPI mode,
    // and are ignored when is_ppi is false (legacy DLT 105 capture).
    add(packet->payload, len, true,
        (int8_t)packet->rx_ctrl.rssi,
        (uint8_t)packet->rx_ctrl.channel);
  }
}

void Buffer::append(String log) {
  bool save_packet = settings_obj.loadSetting<bool>(text_table4[7]);
  if (save_packet) {
    add((const uint8_t*)log.c_str(), log.length(), false);
  }
}

void Buffer::write(int32_t n){
  uint8_t buf[4];
  buf[0] = n;
  buf[1] = n >> 8;
  buf[2] = n >> 16;
  buf[3] = n >> 24;
  write(buf,4);
}

void Buffer::write(uint32_t n){
  uint8_t buf[4];
  buf[0] = n;
  buf[1] = n >> 8;
  buf[2] = n >> 16;
  buf[3] = n >> 24;
  write(buf,4);
}

void Buffer::write(uint16_t n){
  uint8_t buf[2];
  buf[0] = n;
  buf[1] = n >> 8;
  write(buf,2);
}

void Buffer::write(uint8_t n){
  write(&n, 1);
}

void Buffer::write(const uint8_t* buf, uint32_t len){
  if(!writing) return;
  while(saving) delay(10);
  
  if(useA){
    memcpy(&bufA[bufSizeA], buf, len);
    bufSizeA += len;
  }else{
    memcpy(&bufB[bufSizeB], buf, len);
    bufSizeB += len;
  }
}

void Buffer::saveFs(){
  file = fs->open(fileName, FILE_APPEND);
  if (!file) {
    Serial.println(text02+fileName+"'");
    return;
  }

  if(useA){
    if(bufSizeB > 0){
      file.write(bufB, bufSizeB);
    }
    if(bufSizeA > 0){
      file.write(bufA, bufSizeA);
    }
  } else {
    if(bufSizeA > 0){
      file.write(bufA, bufSizeA);
    }
    if(bufSizeB > 0){
      file.write(bufB, bufSizeB);
    }
  }

  file.close();
}

void Buffer::saveSerial() {
  // Saves to main console UART, user-facing app will ignore these markers
  // Uses / and ] in markers as they are illegal characters for SSIDs
  const char* mark_begin = "[BUF/BEGIN]";
  const size_t mark_begin_len = strlen(mark_begin);
  const char* mark_close = "[BUF/CLOSE]";
  const size_t mark_close_len = strlen(mark_close);

  // Additional buffer and memcpy's so that a single Serial.write() is called
  // This is necessary so that other console output isn't mixed into buffer stream
  uint8_t* buf = (uint8_t*)malloc(mark_begin_len + bufSizeA + bufSizeB + mark_close_len);
  uint8_t* it = buf;
  memcpy(it, mark_begin, mark_begin_len);
  it += mark_begin_len;

  if(useA){
    if(bufSizeB > 0){
      memcpy(it, bufB, bufSizeB);
      it += bufSizeB;
    }
    if(bufSizeA > 0){
      memcpy(it, bufA, bufSizeA);
      it += bufSizeA;
    }
  } else {
    if(bufSizeA > 0){
      memcpy(it, bufA, bufSizeA);
      it += bufSizeA;
    }
    if(bufSizeB > 0){
      memcpy(it, bufB, bufSizeB);
      it += bufSizeB;
    }
  }

  memcpy(it, mark_close, mark_close_len);
  it += mark_close_len;
  Serial.write(buf, it - buf);
  free(buf);
}

void Buffer::save() {
  saving = true;

  if((bufSizeA + bufSizeB) == 0){
    saving = false;
    return;
  }

  if(this->fs) saveFs();
  if(this->serial) saveSerial();

  bufSizeA = 0;
  bufSizeB = 0;

  saving = false;
}

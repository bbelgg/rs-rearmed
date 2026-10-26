// efd::DataStream (Gamebryo LightSpeed, efd/DataStream.cpp): the growable byte
// buffer behind message and parameter serialization.
//
// Layout (sizeof 0x38, vtable 0x1018610b0 = {D1 0x1012c2130, D0 0x1012c2190}):
//   +0x00 vptr        +0x08 int32 (zeroed, unused here)
//   +0x10 buf         +0x18 uint32 capacity   +0x1c uint32 size (bytes written)
//   +0x20 pos         read/write cursor, buf <= pos <= buf + size
//   +0x28 endian      shared refcounted {int32 refs; int32 pad; int32 little}
//   +0x30 read_only   +0x31 owns_buffer
//
// Write writes at the cursor and moves the end if it passes it; when
// capacity - size < n it reallocates to max(size + n, capacity + 1024).
// Multi-byte values are byte-swapped when the stream's endian flag
// differs from the host flag (global 0x10186f300, 1 = little-endian): a
// stream made by the constructor is little-endian, so nothing is swapped.
// Errors (read past the end, write to a read-only stream, seek past the end)
// are logged and the operation is skipped.
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "efd_internal.h"

#define DS_FILE RSG_EFD_SRC "DataStream.cpp"

namespace rsg {
namespace efd {

struct EndianFlag {
  int32_t refs;
  int32_t pad;
  int32_t little;
  int32_t pad2;
};

class DataStream {
 public:
  explicit DataStream(uint32_t capacity);
  DataStream(const DataStream &o, bool read_only);
  virtual ~DataStream();

  void Read(void *dst, size_t n);
  void Write(const void *src, size_t n);
  bool Seek(size_t offset);
  void WriteStream(DataStream &o);
  void ReadStream(DataStream &o);
  void WriteID(uint64_t id);
  uint64_t ReadID();

 private:
  bool Swapped() const { return (m_endian->little != 0) != kHostLittle; }
  void SetEndian(EndianFlag *e);
  static const bool kHostLittle = true;

  [[maybe_unused]] int32_t m_unused;  // +0x08, zeroed by the constructors
  uint8_t *m_buf;
  uint32_t m_capacity;
  uint32_t m_size;
  uint8_t *m_pos;
  EndianFlag *m_endian;
  bool m_readOnly;
  bool m_ownsBuffer;

  friend void CheckLayout();
};

void CheckLayout() {
  static_assert(sizeof(DataStream) == RSG_EFD_DATASTREAM_SIZE, "DataStream size");
  static_assert(offsetof(DataStream, m_buf) == 0x10, "buf");
  static_assert(offsetof(DataStream, m_capacity) == 0x18, "capacity");
  static_assert(offsetof(DataStream, m_size) == 0x1c, "size");
  static_assert(offsetof(DataStream, m_pos) == 0x20, "pos");
  static_assert(offsetof(DataStream, m_endian) == 0x28, "endian");
  static_assert(offsetof(DataStream, m_readOnly) == 0x30, "read_only");
  static_assert(offsetof(DataStream, m_ownsBuffer) == 0x31, "owns");
}

static uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }

void DataStream::SetEndian(EndianFlag *e) {
  if (m_endian == e) return;
  if (m_endian && --m_endian->refs == 0) free(m_endian);
  m_endian = e;
  if (e) e->refs++;
}

// 0x1012c1fe0
DataStream::DataStream(uint32_t capacity)
    : m_unused(0), m_capacity(capacity), m_size(0), m_endian(NULL), m_readOnly(false), m_ownsBuffer(true) {
  m_buf = m_pos = (uint8_t *)malloc(capacity ? capacity : 1);
  EndianFlag *e = (EndianFlag *)calloc(1, sizeof *e);
  e->little = 1;
  SetEndian(e);
}

// 0x1012c2080: deep copy of the bytes, shared endian flag.
DataStream::DataStream(const DataStream &o, bool read_only)
    : m_unused(0), m_endian(NULL), m_readOnly(read_only), m_ownsBuffer(true) {
  m_buf = (uint8_t *)malloc(o.m_capacity ? o.m_capacity : 1);
  memcpy(m_buf, o.m_buf, o.m_size);
  m_capacity = o.m_capacity;
  m_pos = m_buf + (o.m_pos - o.m_buf);
  m_size = o.m_size;
  SetEndian(o.m_endian);
}

// 0x1012c2130
DataStream::~DataStream() {
  if (m_ownsBuffer) free(m_buf);
  m_buf = NULL;
  if (m_endian) {
    if (--m_endian->refs == 0) free(m_endian);
    m_endian = NULL;
  }
}

// 0x1012c2200
void DataStream::Read(void *dst, size_t n) {
  if (!n) return;
  if ((size_t)(m_pos + n - m_buf) > m_size) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR1, DS_FILE, 0x88,
                 "Error, request to read buffer past the end of the written data");
    return;
  }
  memcpy(dst, m_pos, n);
  m_pos += n;
}

// 0x1012c2300
void DataStream::Write(const void *src, size_t n) {
  if (m_readOnly) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR1, DS_FILE, 0x98, "Error, write request to a read-only stream");
    return;
  }
  if ((size_t)(m_capacity - m_size) < n) {
    uint32_t at = (uint32_t)(m_pos - m_buf);
    int32_t want = (int32_t)(m_size + (uint32_t)n), grown = (int32_t)(m_capacity + 0x400);
    uint32_t cap = (uint32_t)(want >= grown ? want : grown);
    m_buf = (uint8_t *)realloc(m_buf, cap ? cap : 1);
    m_pos = m_buf + at;
    m_capacity = cap;
  }
  memcpy(m_pos, src, n);
  m_pos += n;
  uint32_t end = (uint32_t)(m_pos - m_buf);
  if (m_size < end) m_size = end;
}

// 0x1012c2460
bool DataStream::Seek(size_t offset) {
  if (offset > m_size) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR1, DS_FILE, 0x13a, "Error, seek beyond end of DataStream");
    return false;
  }
  m_pos = m_buf + offset;
  return true;
}

// 0x1012c2550: a uint32 length prefix, then o's bytes; o becomes read-only.
void DataStream::WriteStream(DataStream &o) {
  if (m_readOnly) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR1, DS_FILE, 0xdf, "Error, write request to a read-only stream");
    return;
  }
  uint32_t len = o.m_size;
  if (Swapped()) len = bswap32(len);
  Write(&len, 4);
  Write(o.m_buf, o.m_size);
  o.m_readOnly = true;
}

// 0x1012c26b0: the inverse; o's cursor is rewound. (If the length itself can't
// be read, the original goes on with an uninitialized length.)
void DataStream::ReadStream(DataStream &o) {
  uint32_t len = 0;
  Read(&len, 4);
  if (Swapped()) len = bswap32(len);
  if ((int64_t)(m_pos - m_buf) + (int64_t)len > (int64_t)m_size) {
    rsg_efd_logf(RSG_EFD_LOG_EFD, RSG_EFD_ERR1, DS_FILE, 0x118,
                 "Error, request to read buffer past the end of the written data");
    return;
  }
  uint8_t *tmp = (uint8_t *)malloc(len ? len : 1);
  Read(tmp, len);
  o.Write(tmp, len);
  o.m_pos = o.m_buf;
  free(tmp);
}

// 0x1013036c0 / 0x101303770: UniversalID as 8 bytes.
void DataStream::WriteID(uint64_t id) {
  if (Swapped()) id = __builtin_bswap64(id);
  Write(&id, 8);
}

uint64_t DataStream::ReadID() {
  uint64_t id = 0;
  Read(&id, 8);
  return Swapped() ? __builtin_bswap64(id) : id;
}

}  // namespace efd
}  // namespace rsg

using rsg::efd::DataStream;

extern "C" {

void rsg_efd_datastream_init(void *ds, uint32_t capacity) { new (ds) DataStream(capacity); }

void rsg_efd_datastream_init_copy(void *ds, const void *other, bool ro) {
  new (ds) DataStream(*(const DataStream *)other, ro);
}

void rsg_efd_datastream_destroy(void *ds) { ((DataStream *)ds)->~DataStream(); }
void rsg_efd_datastream_read(void *ds, void *dst, size_t n) { ((DataStream *)ds)->Read(dst, n); }
void rsg_efd_datastream_write(void *ds, const void *src, size_t n) { ((DataStream *)ds)->Write(src, n); }
bool rsg_efd_datastream_seek(void *ds, size_t off) { return ((DataStream *)ds)->Seek(off); }
void rsg_efd_datastream_write_stream(void *ds, void *o) { ((DataStream *)ds)->WriteStream(*(DataStream *)o); }
void rsg_efd_datastream_read_stream(void *ds, void *o) { ((DataStream *)ds)->ReadStream(*(DataStream *)o); }

void *rsg_efd_datastream_write_id(void *ds, const uint64_t *id) {
  ((DataStream *)ds)->WriteID(*id);
  return ds;
}

void *rsg_efd_datastream_read_id(void *ds, uint64_t *id) {
  // Like the original, a failed read leaves the 8 bytes it would have read
  // undefined; the rewrite stores 0.
  *id = ((DataStream *)ds)->ReadID();
  return ds;
}

}  // extern "C"

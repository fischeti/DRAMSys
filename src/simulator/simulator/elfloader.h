#ifndef _ELFLOADER_H
#define _ELFLOADER_H

#include <cstring>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <map>
#include <iostream>
#include <stdint.h>
#include <stddef.h>

#define ET_EXEC 2
#define EM_RISCV 243
#define EM_NONE 0
#define EV_CURRENT 1

#define IS_ELF(hdr) \
  ((hdr).e_ident[0] == 0x7f && (hdr).e_ident[1] == 'E' && \
   (hdr).e_ident[2] == 'L'  && (hdr).e_ident[3] == 'F')

#define IS_ELF32(hdr) (IS_ELF(hdr) && (hdr).e_ident[4] == 1)
#define IS_ELF64(hdr) (IS_ELF(hdr) && (hdr).e_ident[4] == 2)
#define IS_ELFLE(hdr) (IS_ELF(hdr) && (hdr).e_ident[5] == 1)
#define IS_ELFBE(hdr) (IS_ELF(hdr) && (hdr).e_ident[5] == 2)
#define IS_ELF_EXEC(hdr) (IS_ELF(hdr) && (hdr).e_type == ET_EXEC)
#define IS_ELF_RISCV(hdr) (IS_ELF(hdr) && (hdr).e_machine == EM_RISCV)
#define IS_ELF_EM_NONE(hdr) (IS_ELF(hdr) && (hdr).e_machine == EM_NONE)
#define IS_ELF_VCURRENT(hdr) (IS_ELF(hdr) && (hdr).e_version == EV_CURRENT)

#define PT_LOAD 1

#define SHT_NOBITS 8

typedef struct {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
  uint32_t sh_name;
  uint32_t sh_type;
  uint32_t sh_flags;
  uint32_t sh_addr;
  uint32_t sh_offset;
  uint32_t sh_size;
  uint32_t sh_link;
  uint32_t sh_info;
  uint32_t sh_addralign;
  uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct
{
  uint32_t p_type;
  uint32_t p_offset;
  uint32_t p_vaddr;
  uint32_t p_paddr;
  uint32_t p_filesz;
  uint32_t p_memsz;
  uint32_t p_flags;
  uint32_t p_align;
} Elf32_Phdr;

typedef struct
{
  uint32_t st_name;
  uint32_t st_value;
  uint32_t st_size;
  uint8_t  st_info;
  uint8_t  st_other;
  uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  uint32_t sh_name;
  uint32_t sh_type;
  uint64_t sh_flags;
  uint64_t sh_addr;
  uint64_t sh_offset;
  uint64_t sh_size;
  uint32_t sh_link;
  uint32_t sh_info;
  uint64_t sh_addralign;
  uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
} Elf64_Phdr;

typedef struct {
  uint32_t st_name;
  uint8_t  st_info;
  uint8_t  st_other;
  uint16_t st_shndx;
  uint64_t st_value;
  uint64_t st_size;
} Elf64_Sym;

typedef uint64_t reg_t;
typedef int64_t sreg_t;
typedef reg_t addr_t;

class chunked_memif_t
{
public:
  virtual void read_chunk(addr_t taddr, size_t len, void* dst) = 0;
  virtual void write_chunk(addr_t taddr, size_t len, const void* src) = 0;
  virtual void clear_chunk(addr_t taddr, size_t len) = 0;

  virtual size_t chunk_align() = 0;
  virtual size_t chunk_max_size() = 0;
};

class memif_t
{
public:
  memif_t(chunked_memif_t* _cmemif) : cmemif(_cmemif) {}
  virtual ~memif_t(){}

  // read and write byte arrays
  virtual void read(addr_t addr, size_t len, void* bytes);
  virtual void write(addr_t addr, size_t len, const void* bytes);

  // read and write 8-bit words
  virtual uint8_t read_uint8(addr_t addr);
  virtual int8_t read_int8(addr_t addr);
  virtual void write_uint8(addr_t addr, uint8_t val);
  virtual void write_int8(addr_t addr, int8_t val);

  // read and write 16-bit words
  virtual uint16_t read_uint16(addr_t addr);
  virtual int16_t read_int16(addr_t addr);
  virtual void write_uint16(addr_t addr, uint16_t val);
  virtual void write_int16(addr_t addr, int16_t val);

  // read and write 32-bit words
  virtual uint32_t read_uint32(addr_t addr);
  virtual int32_t read_int32(addr_t addr);
  virtual void write_uint32(addr_t addr, uint32_t val);
  virtual void write_int32(addr_t addr, int32_t val);

  // read and write 64-bit words
  virtual uint64_t read_uint64(addr_t addr);
  virtual int64_t read_int64(addr_t addr);
  virtual void write_uint64(addr_t addr, uint64_t val);
  virtual void write_int64(addr_t addr, int64_t val);

protected:
  chunked_memif_t* cmemif;
};

uint64_t elf_get_symbol_addr(const char* sym);
char elfloader_get_section (long long* address, long long* len);
char elfloader_read_section (long long address, unsigned char * buf);
void elfloader_read_elf(const char* filename, long long dest_size, long long dest_base_addr, unsigned char * dest_buffer);

#endif

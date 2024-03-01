#include "elfloader.h"

#define SHT_PROGBITS 0x1
#define SHT_GROUP 0x11

// address and size
std::vector<std::pair<reg_t, reg_t>> sections;
std::map<std::string, uint64_t> symbols;
// memory based address and content
reg_t entry;
int section_index = 0;

void write (uint64_t address, uint64_t len, uint8_t* buf, long long dest_size, long long dest_base_addr, unsigned char * dest_buffer) {
    std::cout << "elfloader section addr: 0x" << std::hex << address << " len: 0x" << len <<std::endl;
    for (int i = 0; i < len; i++) {
        assert( address - dest_base_addr + i < dest_size);
        dest_buffer[address - dest_base_addr + i] = buf[i];
        // std::cout << "offset 0x" << address - dest_base_addr + i << "= 0x" << std::hex <<(unsigned int)buf[i] <<std::endl;
    }
}

// Communicate the section address and len
// Returns:
// 0 if there are no more sections
// 1 if there are more sections to load
char elfloader_get_section (long long* address, long long* len) {
    if (section_index < sections.size()) {
      *address = sections[section_index].first;
      *len = sections[section_index].second;
      section_index++;
      return 1;
    } else return 0;
}

// Get the address of a symbol defined in the elf file
uint64_t elf_get_symbol_addr(const char* sym) {
    if (symbols.count(sym) == 0) {
      std::cout << "[DRAMSys] Symbol " << sym << " not found" << std::endl;
    }
    return symbols[sym];
}

void elfloader_read_elf(const char* filename, long long dest_size, long long dest_base_addr, unsigned char * dest_buffer) {
  int fd = open(filename, O_RDONLY);
  struct stat s;
  assert(fd != -1);
  if (fstat(fd, &s) < 0)
    abort();
  size_t size = s.st_size;

  char* buf = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(buf != MAP_FAILED);
  close(fd);

  assert(size >= sizeof(Elf64_Ehdr));
  const Elf64_Ehdr* eh64 = (const Elf64_Ehdr*)buf;
  assert(IS_ELF32(*eh64) || IS_ELF64(*eh64));
  assert(IS_ELFLE(*eh64) || IS_ELFBE(*eh64));
  assert(IS_ELF_EXEC(*eh64));
  assert(IS_ELF_RISCV(*eh64) || IS_ELF_EM_NONE(*eh64));
  assert(IS_ELF_VCURRENT(*eh64));

  std::vector<uint8_t> zeros;
  // std::map<std::string, uint64_t> symbols;

  #define LOAD_ELF(ehdr_t, phdr_t, shdr_t, sym_t, bswap) do { \
    ehdr_t* eh = (ehdr_t*)buf; \
    phdr_t* ph = (phdr_t*)(buf + bswap(eh->e_phoff)); \
    entry = bswap(eh->e_entry); \
    assert(size >= bswap(eh->e_phoff) + bswap(eh->e_phnum)*sizeof(*ph)); \
    for (unsigned i = 0; i < bswap(eh->e_phnum); i++) {			\
      if(bswap(ph[i].p_type) == PT_LOAD && bswap(ph[i].p_memsz)) {	\
        if (bswap(ph[i].p_filesz)) {					\
          assert(size >= bswap(ph[i].p_offset) + bswap(ph[i].p_filesz)); \
          write(bswap(ph[i].p_paddr), bswap(ph[i].p_filesz), (uint8_t*)buf + bswap(ph[i].p_offset), dest_size, dest_base_addr, dest_buffer); \
        } \
        zeros.resize(bswap(ph[i].p_memsz) - bswap(ph[i].p_filesz)); \
        write(bswap(ph[i].p_paddr) + bswap(ph[i].p_filesz), bswap(ph[i].p_memsz) - bswap(ph[i].p_filesz), &zeros[0], dest_size, dest_base_addr, dest_buffer); \
      } \
    } \
    shdr_t* sh = (shdr_t*)(buf + bswap(eh->e_shoff)); \
    assert(size >= bswap(eh->e_shoff) + bswap(eh->e_shnum)*sizeof(*sh)); \
    assert(bswap(eh->e_shstrndx) < bswap(eh->e_shnum)); \
    assert(size >= bswap(sh[bswap(eh->e_shstrndx)].sh_offset) + bswap(sh[bswap(eh->e_shstrndx)].sh_size)); \
    char *shstrtab = buf + bswap(sh[bswap(eh->e_shstrndx)].sh_offset);	\
    unsigned strtabidx = 0, symtabidx = 0; \
    for (unsigned i = 0; i < bswap(eh->e_shnum); i++) {		     \
      unsigned max_len = bswap(sh[bswap(eh->e_shstrndx)].sh_size) - bswap(sh[i].sh_name); \
      assert(bswap(sh[i].sh_name) < bswap(sh[bswap(eh->e_shstrndx)].sh_size));	\
      assert(strnlen(shstrtab + bswap(sh[i].sh_name), max_len) < max_len); \
      if (bswap(sh[i].sh_type) & SHT_NOBITS) continue; \
      assert(size >= bswap(sh[i].sh_offset) + bswap(sh[i].sh_size)); \
      if (strcmp(shstrtab + bswap(sh[i].sh_name), ".strtab") == 0) \
        strtabidx = i; \
      if (strcmp(shstrtab + bswap(sh[i].sh_name), ".symtab") == 0) \
        symtabidx = i; \
    } \
    if (strtabidx && symtabidx) { \
      char* strtab = buf + bswap(sh[strtabidx].sh_offset); \
      sym_t* sym = (sym_t*)(buf + bswap(sh[symtabidx].sh_offset)); \
      for (unsigned i = 0; i < bswap(sh[symtabidx].sh_size)/sizeof(sym_t); i++) { \
        unsigned max_len = bswap(sh[strtabidx].sh_size) - bswap(sym[i].st_name); \
        assert(bswap(sym[i].st_name) < bswap(sh[strtabidx].sh_size));	\
        assert(strnlen(strtab + bswap(sym[i].st_name), max_len) < max_len); \
        symbols[strtab + bswap(sym[i].st_name)] = bswap(sym[i].st_value); \
      } \
    } \
  } while(0)

  if (IS_ELFLE(*eh64)) {
    if (IS_ELF32(*eh64))
      LOAD_ELF(Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr, Elf32_Sym, from_le);
    else
      LOAD_ELF(Elf64_Ehdr, Elf64_Phdr, Elf64_Shdr, Elf64_Sym, from_le);
  } else {
    if (IS_ELF32(*eh64))
      LOAD_ELF(Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr, Elf32_Sym, from_be);
    else
      LOAD_ELF(Elf64_Ehdr, Elf64_Phdr, Elf64_Shdr, Elf64_Sym, from_be);
  }

  munmap(buf, size);
}

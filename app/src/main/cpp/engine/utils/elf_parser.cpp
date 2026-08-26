/*
 * NexusVM — ELF Parser Implementation
 */

#include "elf_parser.h"
#include "logger.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace nexusvm {

int ElfParser::parse(uintptr_t base, size_t size) {
    m_base = base;
    m_size = size;
    
    m_ehdr = reinterpret_cast<const Elf64_Ehdr*>(base);
    
    // Validate ELF magic
    if (memcmp(m_ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        LOG_E("Not a valid ELF file (bad magic)");
        return -1;
    }
    
    // Must be ELF64
    if (m_ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        LOG_E("Not an ELF64 file");
        return -2;
    }
    
    // Must be little-endian (ARM64 is LE)
    if (m_ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        LOG_E("Not a little-endian ELF");
        return -3;
    }
    
    // Get program headers
    m_phdr = reinterpret_cast<const Elf64_Phdr*>(base + m_ehdr->e_phoff);
    
    // Parse dynamic section
    return parseDynamic();
}

int ElfParser::parseFile(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        LOG_E("Cannot open ELF file: %s", path.c_str());
        return -1;
    }
    
    off_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    void* mapped = mmap(nullptr, static_cast<size_t>(file_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    
    if (mapped == MAP_FAILED) {
        LOG_E("Failed to mmap ELF file: %s", path.c_str());
        return -2;
    }
    
    int ret = parse(reinterpret_cast<uintptr_t>(mapped), static_cast<size_t>(file_size));
    // Note: we keep the mapping alive for the lifetime of the parser
    return ret;
}

int ElfParser::parseDynamic() {
    // Find the PT_DYNAMIC segment
    const Elf64_Dyn* dynamic = nullptr;
    size_t dynamic_count = 0;
    uintptr_t load_bias = 0;
    
    // First, find the load bias (difference between where the ELF is loaded
    // vs where it expects to be loaded)
    for (int i = 0; i < m_ehdr->e_phnum; ++i) {
        if (m_phdr[i].p_type == PT_LOAD && m_phdr[i].p_offset == 0) {
            load_bias = m_base - m_phdr[i].p_vaddr;
            break;
        }
    }
    
    for (int i = 0; i < m_ehdr->e_phnum; ++i) {
        if (m_phdr[i].p_type == PT_DYNAMIC) {
            dynamic = reinterpret_cast<const Elf64_Dyn*>(m_base + m_phdr[i].p_offset);
            dynamic_count = m_phdr[i].p_memsz / sizeof(Elf64_Dyn);
            break;
        }
    }
    
    if (!dynamic) {
        LOG_W("No PT_DYNAMIC segment found");
        return 0; // Static binary, not an error
    }
    
    // Extract key tables from dynamic entries
    const Elf64_Sym* dynsym = nullptr;
    const char* dynstr = nullptr;
    const Elf64_Rela* rela = nullptr;
    const Elf64_Rela* jmprel = nullptr;
    size_t rela_count = 0;
    size_t jmprel_count = 0;
    size_t dynsym_count = 0;
    
    // Temporary storage for DT entries
    uintptr_t dt_symtab = 0;
    uintptr_t dt_strtab = 0;
    uintptr_t dt_rela = 0;
    size_t dt_relasz = 0;
    uintptr_t dt_jmprel = 0;
    size_t dt_pltrelsz = 0;
    size_t dt_strsz = 0;
    uintptr_t dt_hash = 0;
    uintptr_t dt_gnu_hash = 0;
    
    for (size_t i = 0; i < dynamic_count; ++i) {
        if (dynamic[i].d_tag == DT_NULL) break;
        
        switch (dynamic[i].d_tag) {
            case DT_SYMTAB:    dt_symtab = dynamic[i].d_un.d_ptr; break;
            case DT_STRTAB:    dt_strtab = dynamic[i].d_un.d_ptr; break;
            case DT_RELA:      dt_rela = dynamic[i].d_un.d_ptr; break;
            case DT_RELASZ:    dt_relasz = dynamic[i].d_un.d_val; break;
            case DT_JMPREL:    dt_jmprel = dynamic[i].d_un.d_ptr; break;
            case DT_PLTRELSZ:  dt_pltrelsz = dynamic[i].d_un.d_val; break;
            case DT_STRSZ:     dt_strsz = dynamic[i].d_un.d_val; break;
            case DT_HASH:      dt_hash = dynamic[i].d_un.d_ptr; break;
            case DT_GNU_HASH:  dt_gnu_hash = dynamic[i].d_un.d_ptr; break;
            default: break;
        }
    }
    
    // Resolve pointers (they might be relative to load bias)
    if (dt_symtab) dynsym = reinterpret_cast<const Elf64_Sym*>(load_bias + dt_symtab);
    if (dt_strtab) dynstr = reinterpret_cast<const char*>(load_bias + dt_strtab);
    if (dt_rela)   rela = reinterpret_cast<const Elf64_Rela*>(load_bias + dt_rela);
    if (dt_jmprel) jmprel = reinterpret_cast<const Elf64_Rela*>(load_bias + dt_jmprel);
    
    rela_count = dt_relasz / sizeof(Elf64_Rela);
    jmprel_count = dt_pltrelsz / sizeof(Elf64_Rela);
    
    // Get symbol count from hash table
    if (dt_hash) {
        const uint32_t* hash_table = reinterpret_cast<const uint32_t*>(load_bias + dt_hash);
        // hash_table[0] = nbucket, hash_table[1] = nchain (= nsyms)
        dynsym_count = hash_table[1];
    } else if (dt_gnu_hash) {
        // GNU hash: need to walk the table to find the last symbol
        const uint32_t* gnu_hash = reinterpret_cast<const uint32_t*>(load_bias + dt_gnu_hash);
        uint32_t nbuckets = gnu_hash[0];
        uint32_t symoffset = gnu_hash[1];
        uint32_t bloom_size = gnu_hash[2];
        
        const uint32_t* buckets = reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(gnu_hash) + 16 + bloom_size * 8);
        
        // Find the largest bucket index
        uint32_t max_sym = symoffset;
        for (uint32_t i = 0; i < nbuckets; ++i) {
            if (buckets[i] > max_sym) max_sym = buckets[i];
        }
        
        // Walk the chain from max_sym to find the end
        if (max_sym >= symoffset) {
            const uint32_t* chains = buckets + nbuckets;
            uint32_t chain_idx = max_sym - symoffset;
            while (!(chains[chain_idx] & 1)) {
                ++chain_idx;
                ++max_sym;
            }
            dynsym_count = max_sym + 1;
        }
    }
    
    // Parse symbols
    if (dynsym && dynstr && dynsym_count > 0) {
        parseSymbols(dynsym, dynsym_count, dynstr);
    }
    
    // Parse relocations (GOT — R_AARCH64_GLOB_DAT)
    if (rela && rela_count > 0 && dynstr && dynsym) {
        parseRelocations(rela, rela_count, dynstr, dynsym, false);
    }
    
    // Parse PLT relocations (R_AARCH64_JUMP_SLOT)
    if (jmprel && jmprel_count > 0 && dynstr && dynsym) {
        parseRelocations(jmprel, jmprel_count, dynstr, dynsym, true);
    }
    
    LOG_D("ELF parsed: %zu symbols, %zu PLT relocs, %zu GOT relocs",
          m_symbols.size(), m_plt_relocs.size(), m_got_relocs.size());
    
    return 0;
}

int ElfParser::parseSymbols(const Elf64_Sym* symtab, size_t sym_count, const char* strtab) {
    m_symbols.clear();
    m_symbols.reserve(sym_count);
    
    for (size_t i = 0; i < sym_count; ++i) {
        const Elf64_Sym& sym = symtab[i];
        
        if (sym.st_name == 0) continue; // Skip empty names
        
        ElfSymbol es;
        es.name    = strtab + sym.st_name;
        es.address = sym.st_value;
        es.size    = static_cast<uint32_t>(sym.st_size);
        es.type    = ELF64_ST_TYPE(sym.st_info);
        es.bind    = ELF64_ST_BIND(sym.st_info);
        es.section = sym.st_shndx;
        
        m_symbols.push_back(std::move(es));
    }
    
    return 0;
}

int ElfParser::parseRelocations(const Elf64_Rela* rela, size_t count,
                                 const char* strtab, const Elf64_Sym* symtab,
                                 bool is_plt) {
    auto& target = is_plt ? m_plt_relocs : m_got_relocs;
    target.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        ElfRelocation rel;
        rel.offset    = rela[i].r_offset;
        rel.type      = static_cast<uint32_t>(ELF64_R_TYPE(rela[i].r_info));
        rel.sym_index = static_cast<uint32_t>(ELF64_R_SYM(rela[i].r_info));
        rel.addend    = rela[i].r_addend;
        
        // Resolve symbol name
        if (rel.sym_index > 0) {
            const Elf64_Sym& sym = symtab[rel.sym_index];
            if (sym.st_name > 0) {
                rel.sym_name = strtab + sym.st_name;
            }
        }
        
        target.push_back(std::move(rel));
    }
    
    return 0;
}

// ============================================================================
// Symbol & Relocation Lookup
// ============================================================================

const ElfSymbol* ElfParser::findSymbol(const std::string& name) const {
    for (const auto& sym : m_symbols) {
        if (sym.name == name) {
            return &sym;
        }
    }
    return nullptr;
}

uintptr_t ElfParser::findGotEntry(const std::string& symbol_name) const {
    for (const auto& rel : m_got_relocs) {
        if (rel.sym_name == symbol_name) {
            return m_base + rel.offset;
        }
    }
    return 0;
}

uintptr_t ElfParser::findPltEntry(const std::string& symbol_name) const {
    for (const auto& rel : m_plt_relocs) {
        if (rel.sym_name == symbol_name) {
            return m_base + rel.offset;
        }
    }
    return 0;
}

} // namespace nexusvm

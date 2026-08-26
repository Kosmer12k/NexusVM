/*
 * NexusVM — ELF Parser
 * 
 * Parses ELF64 binaries to find PLT/GOT entries for hooking.
 * Essential for intercepting function calls in guest libraries.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <elf.h>

namespace nexusvm {

/**
 * Represents a symbol found in an ELF binary
 */
struct ElfSymbol {
    std::string name;
    uintptr_t   address;
    uint32_t    size;
    uint8_t     type;       // STT_FUNC, STT_OBJECT, etc.
    uint8_t     bind;       // STB_GLOBAL, STB_WEAK, etc.
    uint16_t    section;    // Section index
};

/**
 * Represents a relocation entry (for PLT/GOT hooking)
 */
struct ElfRelocation {
    uintptr_t   offset;     // Address of the GOT entry
    uint32_t    type;       // Relocation type
    uint32_t    sym_index;  // Symbol index
    int64_t     addend;     // Addend value
    std::string sym_name;   // Resolved symbol name
};

/**
 * ELF64 Parser for ARM64 binaries
 */
class ElfParser {
public:
    ElfParser() = default;
    ~ElfParser() = default;
    
    /**
     * Parse an ELF file from memory (already mmap'd or loaded)
     * @param base Base address of the ELF in memory
     * @param size Size of the ELF mapping
     * @return 0 on success
     */
    int parse(uintptr_t base, size_t size);
    
    /**
     * Parse an ELF file from disk
     * @param path Path to the ELF file
     * @return 0 on success
     */
    int parseFile(const std::string& path);
    
    /**
     * Find a symbol by name
     * @return nullptr if not found
     */
    const ElfSymbol* findSymbol(const std::string& name) const;
    
    /**
     * Get all PLT relocations (R_AARCH64_JUMP_SLOT)
     */
    const std::vector<ElfRelocation>& getPltRelocations() const { return m_plt_relocs; }
    
    /**
     * Get all GOT relocations (R_AARCH64_GLOB_DAT)
     */
    const std::vector<ElfRelocation>& getGotRelocations() const { return m_got_relocs; }
    
    /**
     * Find the GOT entry address for a given symbol name
     * @return 0 if not found
     */
    uintptr_t findGotEntry(const std::string& symbol_name) const;
    
    /**
     * Find the PLT entry address for a given symbol name
     * @return 0 if not found
     */
    uintptr_t findPltEntry(const std::string& symbol_name) const;
    
    /**
     * Get all exported symbols
     */
    const std::vector<ElfSymbol>& getSymbols() const { return m_symbols; }
    
    /**
     * Get the ELF base address
     */
    uintptr_t getBase() const { return m_base; }

private:
    int parseDynamic();
    int parseSymbols(const Elf64_Sym* symtab, size_t sym_count, const char* strtab);
    int parseRelocations(const Elf64_Rela* rela, size_t count, const char* strtab, 
                         const Elf64_Sym* symtab, bool is_plt);
    
    uintptr_t               m_base = 0;
    size_t                  m_size = 0;
    const Elf64_Ehdr*       m_ehdr = nullptr;
    const Elf64_Phdr*       m_phdr = nullptr;
    
    std::vector<ElfSymbol>      m_symbols;
    std::vector<ElfRelocation>  m_plt_relocs;
    std::vector<ElfRelocation>  m_got_relocs;
};

} // namespace nexusvm

#ifndef UNIT_MACHO_LOCAL_H
#define UNIT_MACHO_LOCAL_H

#include <stdint.h>

// Vendored Mach-O structures for cross-compilation support.
// These mirror the standard Mach-O 64-bit definitions.

// Mach-O magic numbers
enum {
    MACHO_MAGIC_64 = 0xFEEDFACF,
};

// CPU types
enum {
    MACHO_CPU_TYPE_X86_64  = 0x01000007,  // CPU_TYPE_X86_64
    MACHO_CPU_TYPE_ARM64   = 0x0100000C,  // CPU_TYPE_ARM64
};

// CPU subtypes
enum {
    MACHO_CPU_SUBTYPE_ALL    = 0x00000003,
    MACHO_CPU_SUBTYPE_ARM64_ALL = 0x00000000,
};

// File types
enum {
    MACHO_FILETYPE_OBJECT  = 1,   // MH_OBJECT
};

// Mach-O header flags
enum {
    MACHO_FLAG_SUBSECTIONS_VIA_SYMBOLS = 0x00002000,
};

// Load command types
enum {
    MACHO_LC_SEGMENT_64  = 0x19,
    MACHO_LC_SYMTAB      = 0x02,
    MACHO_LC_DYSYMTAB    = 0x0B,
    MACHO_LC_BUILD_VERSION = 0x32,
};

// Section types and attributes
enum {
    MACHO_S_REGULAR          = 0x0,
    MACHO_S_CSTRING_LITERALS = 0x2,

    MACHO_S_ATTR_PURE_INSTRUCTIONS = 0x80000000,
    MACHO_S_ATTR_SOME_INSTRUCTIONS = 0x00000400,
};

// Relocation types for ARM64
enum {
    MACHO_ARM64_RELOC_UNSIGNED       = 0,
    MACHO_ARM64_RELOC_SUBTRACTOR     = 1,
    MACHO_ARM64_RELOC_BRANCH26       = 2,
    MACHO_ARM64_RELOC_PAGE21         = 3,
    MACHO_ARM64_RELOC_PAGEOFF12      = 4,
    MACHO_ARM64_RELOC_GOT_LOAD_PAGE21 = 5,
    MACHO_ARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6,
    MACHO_ARM64_RELOC_POINTER_TO_GOT = 7,
    MACHO_ARM64_RELOC_TLVP_LOAD_PAGE21 = 8,
    MACHO_ARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9,
    MACHO_ARM64_RELOC_ADDEND = 10,
};

// Relocation types for x86_64
enum {
    MACHO_X86_64_RELOC_UNSIGNED  = 0,
    MACHO_X86_64_RELOC_SIGNED    = 1,
    MACHO_X86_64_RELOC_BRANCH    = 2,
    MACHO_X86_64_RELOC_GOT_LOAD  = 3,
    MACHO_X86_64_RELOC_GOT       = 4,
    MACHO_X86_64_RELOC_SUBTRACTOR = 5,
    MACHO_X86_64_RELOC_SIGNED_1  = 6,
    MACHO_X86_64_RELOC_SIGNED_2  = 7,
    MACHO_X86_64_RELOC_SIGNED_4  = 8,
};

// N_EXT, N_SECT, etc.
enum {
    MACHO_N_UNDF = 0x0,
    MACHO_N_EXT  = 0x1,
    MACHO_N_SECT = 0xE,
};

// Reference types for undefined symbols
enum {
    MACHO_REFERENCE_FLAG_UNDEFINED_NON_LAZY = 0x0000,
};

// NO_SECT
enum {
    MACHO_NO_SECT = 0,
};

// Mach-O header (64-bit)
typedef struct {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} MachO_Header;

// Segment command (64-bit)
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} MachO_SegmentCommand;

// Section (64-bit)
typedef struct {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} MachO_Section;

// Symbol table command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} MachO_SymtabCommand;

// Dysymtab command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
} MachO_DysymtabCommand;

// Build version command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
} MachO_BuildVersionCommand;

// nlist_64 (symbol table entry)
typedef struct {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
} MachO_Nlist64;

// Relocation entry
typedef struct {
    int32_t  r_address;
    uint32_t r_info;  // symbolnum:24, pcrel:1, length:2, extern:1, type:4
} MachO_RelocationInfo;

#define MACHO_RELOC_INFO(symbolnum, pcrel, length, is_extern, type) \
    ((uint32_t)(((symbolnum) & 0x00FFFFFF)       \
    | (((pcrel) & 1) << 24)                      \
    | (((length) & 3) << 25)                     \
    | (((is_extern) & 1) << 27)                  \
    | (((type) & 0xF) << 28)))

_Static_assert(sizeof(MachO_Header) == 32,
               "MachO_Header must be 32 bytes");
_Static_assert(sizeof(MachO_SegmentCommand) == 72,
               "MachO_SegmentCommand must be 72 bytes");
_Static_assert(sizeof(MachO_Section) == 80,
               "MachO_Section must be 80 bytes");
_Static_assert(sizeof(MachO_SymtabCommand) == 24,
               "MachO_SymtabCommand must be 24 bytes");
_Static_assert(sizeof(MachO_Nlist64) == 16,
               "MachO_Nlist64 must be 16 bytes");
_Static_assert(sizeof(MachO_RelocationInfo) == 8,
               "MachO_RelocationInfo must be 8 bytes");

#endif
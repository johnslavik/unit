// ================================================================
// HOW MACH-O WORKS
// ================================================================
//
// A Mach-O (Mach Object) file is the standard binary format on
// macOS, iOS, and other Apple platforms. For relocatable object
// files (.o), the layout is:
//
//   +-------------------+
//   | Mach-O Header     |  32 bytes (64-bit). Contains the magic
//   |                   |  number (0xFEEDFACF), CPU type, file
//   |                   |  type (MH_OBJECT), and the number/size
//   |                   |  of load commands.
//   +-------------------+
//   | Load Commands     |  Variable-length list of commands:
//   |   LC_SEGMENT_64   |  - Describes one segment and its
//   |                   |    sections (__TEXT,__text and
//   |                   |    __TEXT,__cstring).
//   |   LC_SYMTAB       |  - Symbol table location and count.
//   |   LC_DYSYMTAB     |  - Dynamic symbol table info.
//   |   LC_BUILD_VERSION|  - Build/platform metadata.
//   +-------------------+
//   | __TEXT,__text      |  Machine code. Marked executable.
//   +-------------------+
//   | __TEXT,__cstring   |  Read-only string constants.
//   +-------------------+
//   | Relocations       |  Fix-up entries for the linker.
//   +-------------------+
//   | Symbol Table      |  nlist_64 entries.
//   +-------------------+
//   | String Table      |  Null-terminated symbol name strings.
//   +-------------------+
//
// KEY DIFFERENCES FROM ELF:
//
//   - Mach-O symbol names are prefixed with '_' (C convention).
//   - Relocations are per-section, not in separate sections.
//   - The segment/section model replaces ELF's flat section list.
//   - ARM64 relocations use different types (BRANCH26, etc.).
//
// ================================================================

#include <stdio.h>
#include <string.h>

#include <unit/base.h>
#include <unit/errors.h>
#include <unit/platform.h>

#include <unit/internal/code_buffer.h>
#include <unit/internal/compile_context.h>
#include <unit/internal/executable_formats.h>
#include <unit/internal/size_map.h>
#include <unit/internal/vector.h>

#include "macho_local.h"

typedef struct {
    UNIT_Context *context;
    UNIT_Architecture arch;
    MachO_Header header;
    MachO_SegmentCommand segment;
    MachO_Section text_section;
    MachO_Section cstring_section;
    MachO_SymtabCommand symtab_cmd;
    MachO_DysymtabCommand dysymtab_cmd;
    MachO_BuildVersionCommand build_version_cmd;
    _UNIT_Vector symbols;       // MachO_Nlist64*
    _UNIT_Vector string_table;  // char*
    _UNIT_Vector relocations;   // MachO_RelocationInfo*
    const _UNIT_CodeBuffer *text;
    const _UNIT_CodeBuffer *constant_data;
    _UNIT_SizeMap symtab_indices;
    UNIT_Size num_local_syms;
    UNIT_Size num_extdef_syms;
    UNIT_Size num_undef_syms;
} _UNIT_MachO_Object;

// Append a null-terminated string to the string table.
// Returns the byte offset where the string starts.
static UNIT_Size
append_string(_UNIT_MachO_Object *object, const char *string)
{
    assert(object != NULL);
    assert(string != NULL);
    _UNIT_Vector *string_table = &object->string_table;
    UNIT_Size offset = _UNIT_Vector_SIZE(string_table);
    UNIT_Size length = strlen(string);

    for (UNIT_Size index = 0; index <= length; ++index) {
        char *character = _UNIT_Alloc(object->context, sizeof(char));
        if (character == NULL) {
            return -1;
        }
        *character = string[index];
        if (UNIT_FAILED(_UNIT_Vector_Append(string_table, character))) {
            return -1;
        }
    }

    return offset;
}

// Append a string with '_' prefix (Mach-O C symbol convention).
static UNIT_Size
append_symbol_name(_UNIT_MachO_Object *object, const char *name)
{
    assert(object != NULL);
    assert(name != NULL);
    _UNIT_Vector *string_table = &object->string_table;
    UNIT_Size offset = _UNIT_Vector_SIZE(string_table);

    // Prepend '_'
    char *underscore = _UNIT_Alloc(object->context, sizeof(char));
    if (underscore == NULL) return -1;
    *underscore = '_';
    if (UNIT_FAILED(_UNIT_Vector_Append(string_table, underscore))) return -1;

    UNIT_Size length = strlen(name);
    for (UNIT_Size index = 0; index <= length; ++index) {
        char *character = _UNIT_Alloc(object->context, sizeof(char));
        if (character == NULL) return -1;
        *character = name[index];
        if (UNIT_FAILED(_UNIT_Vector_Append(string_table, character))) return -1;
    }

    return offset;
}

static MachO_Nlist64 *
create_and_store_symbol(_UNIT_MachO_Object *object)
{
    assert(object != NULL);
    MachO_Nlist64 *symbol = _UNIT_Alloc(object->context, sizeof(MachO_Nlist64));
    if (symbol == NULL) return NULL;
    memset(symbol, 0, sizeof(MachO_Nlist64));
    if (UNIT_FAILED(_UNIT_Vector_Append(&object->symbols, symbol))) return NULL;
    return symbol;
}

static UNIT_Status
add_symbols(_UNIT_MachO_Object *object, const _UNIT_CompileContext *compile_context)
{
    const _UNIT_Vector *symbols = &compile_context->symbol_table.symbols;
    UNIT_Size count = _UNIT_Vector_SIZE(symbols);

    // First pass: count defined and undefined symbols
    UNIT_Size num_defined = 0;
    UNIT_Size num_undefined = 0;
    for (UNIT_Size index = 0; index < count; ++index) {
        _UNIT_Symbol *symbol = _UNIT_Vector_GET(symbols, index);
        if (symbol->is_defined) {
            num_defined++;
        } else {
            num_undefined++;
        }
    }

    // We emit defined symbols first (as external definitions), then undefined.
    // This is important for dysymtab.
    object->num_local_syms = 0;
    object->num_extdef_syms = num_defined;
    object->num_undef_syms = num_undefined;

    UNIT_Size table_index = 0;

    // Pass 1: defined symbols
    for (UNIT_Size index = 0; index < count; ++index) {
        _UNIT_Symbol *symbol = _UNIT_Vector_GET(symbols, index);
        if (!symbol->is_defined) continue;

        MachO_Nlist64 *nlist = create_and_store_symbol(object);
        if (nlist == NULL) return _UNIT_FAIL;

        nlist->n_strx = append_symbol_name(object, symbol->name);
        if (nlist->n_strx == (uint32_t)-1) return _UNIT_FAIL;

        nlist->n_type = MACHO_N_SECT | MACHO_N_EXT;
        nlist->n_sect = 1; // __text is section 1
        nlist->n_value = symbol->text_offset;

        if (UNIT_FAILED(_UNIT_SizeMap_Set(&object->symtab_indices, index,
                                          table_index++))) {
            return _UNIT_FAIL;
        }
    }

    // Pass 2: undefined symbols
    for (UNIT_Size index = 0; index < count; ++index) {
        _UNIT_Symbol *symbol = _UNIT_Vector_GET(symbols, index);
        if (symbol->is_defined) continue;

        MachO_Nlist64 *nlist = create_and_store_symbol(object);
        if (nlist == NULL) return _UNIT_FAIL;

        nlist->n_strx = append_symbol_name(object, symbol->name);
        if (nlist->n_strx == (uint32_t)-1) return _UNIT_FAIL;

        nlist->n_type = MACHO_N_EXT;
        nlist->n_sect = MACHO_NO_SECT;
        nlist->n_desc = MACHO_REFERENCE_FLAG_UNDEFINED_NON_LAZY;

        if (UNIT_FAILED(_UNIT_SizeMap_Set(&object->symtab_indices, index,
                                          table_index++))) {
            return _UNIT_FAIL;
        }
    }

    return _UNIT_OK;
}

static UNIT_Status
build_relocations(_UNIT_MachO_Object *object,
                  const _UNIT_CompileContext *compile_context)
{
    const _UNIT_Vector *relocations = &compile_context->symbol_table.relocations;
    UNIT_Size count = _UNIT_Vector_SIZE(relocations);
    int is_arm64 = (object->arch == UNIT_ARCH_AARCH64);

    for (UNIT_Size index = 0; index < count; ++index) {
        _UNIT_Relocation *relocation = _UNIT_Vector_GET(relocations, index);

        MachO_RelocationInfo *entry = _UNIT_Alloc(object->context,
                                                   sizeof(MachO_RelocationInfo));
        if (entry == NULL) return _UNIT_FAIL;
        memset(entry, 0, sizeof(MachO_RelocationInfo));

        if (relocation->type == RELOCATION_CALL) {
            UNIT_Size resolved_index = _UNIT_SizeMap_GET(&object->symtab_indices,
                                                         relocation->symbol_index);
            entry->r_address = (int32_t)relocation->offset;

            if (is_arm64) {
                // ARM64 BL instruction: BRANCH26 relocation, length=2 (4 bytes)
                entry->r_info = MACHO_RELOC_INFO(resolved_index, 1, 2, 1,
                                                  MACHO_ARM64_RELOC_BRANCH26);
            } else {
                // x86_64: BRANCH relocation, length=2 (4 bytes), PC-relative
                entry->r_info = MACHO_RELOC_INFO(resolved_index, 1, 2, 1,
                                                  MACHO_X86_64_RELOC_BRANCH);
            }
        } else if (relocation->type == RELOCATION_DATA) {
            // Data relocations reference the __cstring section directly.
            // For Mach-O object files, the data is within the same segment
            // so we skip emitting these relocations — the JIT resolver
            // handles them directly at runtime. In an object file, the
            // code and data are already correctly placed.
            // We skip data relocations in the .o output.
            continue;
        }

        if (UNIT_FAILED(_UNIT_Vector_Append(&object->relocations, entry))) {
            return _UNIT_FAIL;
        }
    }

    return _UNIT_OK;
}

static UNIT_Status
build_macho_object(_UNIT_MachO_Object *object,
                   const _UNIT_CompileContext *compile_context,
                   UNIT_Architecture arch)
{
    assert(object != NULL);
    assert(compile_context != NULL);
    object->context = compile_context->context;
    object->arch = arch;
    object->text = &compile_context->buffer;
    object->constant_data = &compile_context->string_data.constant_buffer;

    if (UNIT_FAILED(_UNIT_Vector_Init(&object->string_table,
                                      compile_context->context,
                                      8, _UNIT_Dealloc))) {
        return _UNIT_FAIL;
    }

    if (UNIT_FAILED(_UNIT_Vector_Init(&object->symbols,
                                      compile_context->context,
                                      16, _UNIT_Dealloc))) {
        _UNIT_Vector_Clear(&object->string_table);
        return _UNIT_FAIL;
    }

    if (UNIT_FAILED(_UNIT_Vector_Init(&object->relocations,
                                      compile_context->context, 16, _UNIT_Dealloc))) {
        _UNIT_Vector_Clear(&object->string_table);
        _UNIT_Vector_Clear(&object->symbols);
        return _UNIT_FAIL;
    }

    if (UNIT_FAILED(_UNIT_SizeMap_Init(&object->symtab_indices,
                                       compile_context->context, 8))) {
        _UNIT_Vector_Clear(&object->string_table);
        _UNIT_Vector_Clear(&object->symbols);
        _UNIT_Vector_Clear(&object->relocations);
        return _UNIT_FAIL;
    }

    // String table starts with a single null byte (index 0 = empty string)
    if (append_string(object, "") == -1) goto error;

    // Add all symbols
    if (UNIT_FAILED(add_symbols(object, compile_context))) goto error;

    // Build relocations (only call relocations for Mach-O object files)
    if (UNIT_FAILED(build_relocations(object, compile_context))) goto error;

    // ================================================================
    // Compute layout
    // ================================================================

    UNIT_Size text_size = _UNIT_CodeBuffer_CurrentIndex(object->text);
    UNIT_Size cstring_size = object->constant_data->size;
    UNIT_Size reloc_count = _UNIT_Vector_SIZE(&object->relocations);
    UNIT_Size symbol_count = _UNIT_Vector_SIZE(&object->symbols);
    UNIT_Size string_table_size = _UNIT_Vector_SIZE(&object->string_table);

    int has_cstring = (cstring_size > 0);
    UNIT_Size num_sections = has_cstring ? 2 : 1;

    // Load commands
    UNIT_Size segment_cmd_size = sizeof(MachO_SegmentCommand)
                               + num_sections * sizeof(MachO_Section);
    UNIT_Size symtab_cmd_size = sizeof(MachO_SymtabCommand);
    UNIT_Size dysymtab_cmd_size = sizeof(MachO_DysymtabCommand);
    UNIT_Size build_version_cmd_size = sizeof(MachO_BuildVersionCommand);
    UNIT_Size total_cmd_size = segment_cmd_size + symtab_cmd_size
                             + dysymtab_cmd_size + build_version_cmd_size;

    UNIT_Size header_and_cmds = sizeof(MachO_Header) + total_cmd_size;

    // Section data starts right after headers + load commands
    UNIT_Size text_offset = header_and_cmds;
    UNIT_Size cstring_offset = text_offset + text_size;
    UNIT_Size section_data_end = cstring_offset + cstring_size;

    // Relocations (4-byte aligned)
    UNIT_Size reloc_offset = (section_data_end + 3) & ~3;
    UNIT_Size reloc_size = reloc_count * sizeof(MachO_RelocationInfo);

    // Symbol table (after relocations)
    UNIT_Size symtab_offset = reloc_offset + reloc_size;
    UNIT_Size symtab_size = symbol_count * sizeof(MachO_Nlist64);

    // String table (after symbol table)
    UNIT_Size strtab_offset = symtab_offset + symtab_size;

    UNIT_Size total_segment_data = text_size + cstring_size;

    // Fill in the header
    object->header = (MachO_Header) {
        .magic = MACHO_MAGIC_64,
        .cputype = (arch == UNIT_ARCH_AARCH64) ? MACHO_CPU_TYPE_ARM64
                                                : MACHO_CPU_TYPE_X86_64,
        .cpusubtype = (arch == UNIT_ARCH_AARCH64) ? MACHO_CPU_SUBTYPE_ARM64_ALL
                                                   : MACHO_CPU_SUBTYPE_ALL,
        .filetype = MACHO_FILETYPE_OBJECT,
        .ncmds = 4, // segment + symtab + dysymtab + build_version
        .sizeofcmds = (uint32_t)total_cmd_size,
        .flags = MACHO_FLAG_SUBSECTIONS_VIA_SYMBOLS,
        .reserved = 0,
    };

    // Segment command
    object->segment = (MachO_SegmentCommand) {
        .cmd = MACHO_LC_SEGMENT_64,
        .cmdsize = (uint32_t)segment_cmd_size,
        .vmaddr = 0,
        .vmsize = total_segment_data,
        .fileoff = text_offset,
        .filesize = total_segment_data,
        .maxprot = 7, // rwx
        .initprot = 7,
        .nsects = (uint32_t)num_sections,
        .flags = 0,
    };
    memset(object->segment.segname, 0, 16);

    // __TEXT,__text section
    memset(&object->text_section, 0, sizeof(MachO_Section));
    memcpy(object->text_section.sectname, "__text", 6);
    memcpy(object->text_section.segname, "__TEXT", 6);
    object->text_section.addr = 0;
    object->text_section.size = text_size;
    object->text_section.offset = (uint32_t)text_offset;
    object->text_section.align = 2; // 2^2 = 4 byte alignment (good for both)
    object->text_section.reloff = reloc_count > 0 ? (uint32_t)reloc_offset : 0;
    object->text_section.nreloc = (uint32_t)reloc_count;
    object->text_section.flags = MACHO_S_ATTR_PURE_INSTRUCTIONS
                               | MACHO_S_ATTR_SOME_INSTRUCTIONS;

    // __TEXT,__cstring section
    if (has_cstring) {
        memset(&object->cstring_section, 0, sizeof(MachO_Section));
        memcpy(object->cstring_section.sectname, "__cstring", 9);
        memcpy(object->cstring_section.segname, "__TEXT", 6);
        object->cstring_section.addr = text_size;
        object->cstring_section.size = cstring_size;
        object->cstring_section.offset = (uint32_t)cstring_offset;
        object->cstring_section.align = 0;
        object->cstring_section.flags = MACHO_S_CSTRING_LITERALS;
    }

    // Symtab command
    object->symtab_cmd = (MachO_SymtabCommand) {
        .cmd = MACHO_LC_SYMTAB,
        .cmdsize = (uint32_t)symtab_cmd_size,
        .symoff = (uint32_t)symtab_offset,
        .nsyms = (uint32_t)symbol_count,
        .stroff = (uint32_t)strtab_offset,
        .strsize = (uint32_t)string_table_size,
    };

    // Dysymtab command
    memset(&object->dysymtab_cmd, 0, sizeof(MachO_DysymtabCommand));
    object->dysymtab_cmd.cmd = MACHO_LC_DYSYMTAB;
    object->dysymtab_cmd.cmdsize = (uint32_t)dysymtab_cmd_size;
    object->dysymtab_cmd.ilocalsym = 0;
    object->dysymtab_cmd.nlocalsym = (uint32_t)object->num_local_syms;
    object->dysymtab_cmd.iextdefsym = (uint32_t)object->num_local_syms;
    object->dysymtab_cmd.nextdefsym = (uint32_t)object->num_extdef_syms;
    object->dysymtab_cmd.iundefsym = (uint32_t)(object->num_local_syms + object->num_extdef_syms);
    object->dysymtab_cmd.nundefsym = (uint32_t)object->num_undef_syms;

    // Build version command (macOS 11.0, platform=1)
    object->build_version_cmd = (MachO_BuildVersionCommand) {
        .cmd = MACHO_LC_BUILD_VERSION,
        .cmdsize = (uint32_t)build_version_cmd_size,
        .platform = 1,  // PLATFORM_MACOS
        .minos = 0x000B0000,  // 11.0.0
        .sdk = 0x000E0000,    // 14.0.0
        .ntools = 0,
    };

    return _UNIT_OK;

error:
    _UNIT_Vector_Clear(&object->string_table);
    _UNIT_Vector_Clear(&object->symbols);
    _UNIT_Vector_Clear(&object->relocations);
    _UNIT_SizeMap_Clear(&object->symtab_indices);
    return _UNIT_FAIL;
}

static UNIT_Status
write_object_to_file(const _UNIT_MachO_Object *object, const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        _UNIT_SetOSError(object->context, "writing Mach-O object");
        return _UNIT_FAIL;
    }

    int has_cstring = (object->constant_data->size > 0);

    // Header
    fwrite(&object->header, sizeof(object->header), 1, file);

    // Load commands
    fwrite(&object->segment, sizeof(object->segment), 1, file);
    fwrite(&object->text_section, sizeof(object->text_section), 1, file);
    if (has_cstring) {
        fwrite(&object->cstring_section, sizeof(object->cstring_section), 1, file);
    }
    fwrite(&object->symtab_cmd, sizeof(object->symtab_cmd), 1, file);
    fwrite(&object->dysymtab_cmd, sizeof(object->dysymtab_cmd), 1, file);
    fwrite(&object->build_version_cmd, sizeof(object->build_version_cmd), 1, file);

    // __text data
    fwrite(object->text->data, 1, object->text->size, file);

    // __cstring data
    if (has_cstring) {
        fwrite(object->constant_data->data, 1, object->constant_data->size, file);
    }

    // Padding to alignment for relocations
    UNIT_Size section_data_end = (UNIT_Size)ftell(file);
    UNIT_Size reloc_offset_aligned = (section_data_end + 3) & ~3;
    while ((UNIT_Size)ftell(file) < reloc_offset_aligned) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, file);
    }

    // Relocations
    UNIT_Size reloc_count = _UNIT_Vector_SIZE(&object->relocations);
    for (UNIT_Size index = 0; index < reloc_count; ++index) {
        MachO_RelocationInfo *entry = _UNIT_Vector_GET(&object->relocations, index);
        fwrite(entry, sizeof(MachO_RelocationInfo), 1, file);
    }

    // Symbol table
    UNIT_Size symbol_count = _UNIT_Vector_SIZE(&object->symbols);
    for (UNIT_Size index = 0; index < symbol_count; ++index) {
        MachO_Nlist64 *symbol = _UNIT_Vector_GET(&object->symbols, index);
        fwrite(symbol, sizeof(MachO_Nlist64), 1, file);
    }

    // String table
    UNIT_Size string_table_size = _UNIT_Vector_SIZE(&object->string_table);
    for (UNIT_Size index = 0; index < string_table_size; ++index) {
        char *character = _UNIT_Vector_GET(&object->string_table, index);
        fwrite(character, 1, 1, file);
    }

    fclose(file);
    return _UNIT_OK;
}

UNIT_Status
_UNIT_MachO_WriteObjectFile(const _UNIT_CompileContext *context,
                            const char *path,
                            UNIT_Architecture arch)
{
    assert(context != NULL);
    assert(path != NULL);
    _UNIT_MachO_Object macho_object;
    if (UNIT_FAILED(build_macho_object(&macho_object, context, arch))) {
        return _UNIT_FAIL;
    }

    if (UNIT_FAILED(write_object_to_file(&macho_object, path))) {
        return _UNIT_FAIL;
    }

    _UNIT_SizeMap_Clear(&macho_object.symtab_indices);
    _UNIT_Vector_Clear(&macho_object.relocations);
    _UNIT_Vector_Clear(&macho_object.string_table);
    _UNIT_Vector_Clear(&macho_object.symbols);

    return _UNIT_OK;
}
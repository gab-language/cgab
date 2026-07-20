#include "core.h"
#include "platform.h"

#define PACKAGE "obj2stencil"
#define PACKAGE_VERSION "0.1.0"
#include <bfd.h>

#include <stddef.h>
#include <stdio.h>

#define STR(m) #m
const char *micro_op_sections[] = {
#define IR_CODE(name) STR(.text.OP_MICRO_OP_##name##_HANDLER),
#include "ir.h"
#undef IR_CODE
};
#undef STR

bool scn_isop(const char *scn_name) {
  for (size_t i = 0; i < LEN_CARRAY(micro_op_sections); i++) {
    if (!strcmp(scn_name, micro_op_sections[i]))
      return true;
  }

  return false;
}

const char *symtoknownhole(const char *symbol) {
  if (!strcmp(symbol, "_jit_arg1"))
    return "kGAB_JIT_RELOC_32ARG1";

  if (!strcmp(symbol, "_jit_arg2"))
    return "kGAB_JIT_RELOC_32ARG2";

  if (!strcmp(symbol, "_jit_arg1_large"))
    return "kGAB_JIT_RELOC_64ARG1";

  if (!strcmp(symbol, "_jit_arg2_large"))
    return "kGAB_JIT_RELOC_64ARG2";

  if (!strcmp(symbol, "_jit_arg_ip"))
    return "kGAB_JIT_RELOC_IP";

  if (!strcmp(symbol, "_jit_arg_hv"))
    return "kGAB_JIT_RELOC_HV";

  if (!strcmp(symbol, "_jit_bail"))
    return "kGAB_JIT_RELOC_BAIL";

  if (!strcmp(symbol, "_jit_next"))
    return "kGAB_JIT_RELOC_NEXT";

  if (!strcmp(symbol, "_jit_exit"))
    return "kGAB_JIT_RELOC_EXIT";

  return "kGAB_JIT_RELOC_TRMP";
}

void emit_scn(bfd *bfd, struct bfd_symbol *sym) {
  const char *scn_name = sym->section->name;

  assert(sym->section->flags & SEC_IN_MEMORY);

  size_t scn_name_len = strlen(scn_name);

  // Comment Description
  printf("\n/* EMIT %.*s */\n\n", (int)scn_name_len - 6, scn_name + 6);

  // Stencil's code.
  printf("uint8_t %.*s_BYTES[] = {\n\t", (int)scn_name_len - 6, scn_name + 6);
  size_t bytes_len = sym->section->size;
  for (size_t i = 0; i < bytes_len; i++) {
    unsigned char byte = ((unsigned char *)sym->section->contents)[i];
    printf("0x%02x, ", byte);
  }
  printf("\n};\n");

  printf("struct gab_jit_reloc %.*s_RELAS[] = {\n", (int)scn_name_len - 6,
         scn_name + 6);
  for (size_t i = 0; i < sym->section->reloc_count; i++) {
    struct reloc_cache_entry rela = sym->section->relocation[i];

    if (rela.addend >= bytes_len)
      continue;

    struct bfd_symbol* sym = *rela.sym_ptr_ptr;

    rela.howto
    if (sym.st_shndx == SHN_UNDEF) {
      const char *known_hole = symtoknownhole(symname);
      assert(known_hole);

      printf("\t{ %s, %lu, %li, { .trampoline = { \"%s\" } } },\n", known_hole,
             rela.r_offset, rela.r_addend, symname);
    } else {
      /*
       * String constant's sizes aren't copied correctly here
       *
       * That is an issue 4 sure.
       */

      struct bfd_section *scn = elf_getscn(elf, sym.st_shndx);

      Elf_Data *data = elf_getdata(scn, nullptr);
      const unsigned char *src = (unsigned char *)data->d_buf + sym.st_value;

      size_t len = data->d_align == 1 ? data->d_size : data->d_align;

      int type = ELF64_R_TYPE(rela.r_info);

      static const char *rela_types[] = {
          [R_X86_64_64] = "kGAB_JIT_RELOC_CONST_ABSOLUTE",
          [R_X86_64_32] = "kGAB_JIT_RELOC_CONST_ABSOLUTE",
          [R_X86_64_PC64] = "kGAB_JIT_RELOC_CONST_RELATIVE",
          [R_X86_64_PC32] = "kGAB_JIT_RELOC_CONST_RELATIVE",
          [R_X86_64_PLT32] = "kGAB_JIT_RELOC_CONST_RELATIVE",
      };

      assert(type < sizeof(rela_types) / sizeof(rela_types[0]));

      const char *stype = rela_types[type];
      assert(stype);

      printf("\t{ %s, %lu, %li, ", stype, rela.r_offset, rela.r_addend);
      if (len) {
        assert(len < UINT8_MAX);

        printf("{ .constant = { %lu, { ", len);
        for (size_t j = 0; j < len; j++) {
          printf("0x%02x, ", src[j]);
        }
        printf("} } }");
      }
      printf("},\n");
    }
  }
  printf("};\n");
}

int main(int argc, const char **argv) {
  if (gab_osfisatty(stdin)) {
    fprintf(stderr, "[ERR]: stdin is not a file.\n");
    return -1;
  }

  if (!bfd_init()) {
    fprintf(stderr, "[BFD]: %s\n", bfd_errmsg(errno));
    return -1;
  }

  bfd *bfd = bfd_fdopenr("stdin", "", gab_osfileno(stdin));

  if (!bfd) {
    fprintf(stderr, "[BFD]: %s\n", bfd_errmsg(errno));
    return -1;
  }

  fprintf(stdout, "[BFD]: Flavor: %s\n", bfd_flavour_name(bfd_flavour(bfd)));

  int64_t storage_needed = bfd_get_symtab_upper_bound(bfd);

  if (storage_needed < 0) {
    fprintf(stderr, "[BFD]: %s\n", bfd_errmsg(errno));
    return -1;
  }

  if (!storage_needed)
    return 0;

  bfd_symbol** symtab = malloc(storage_needed);

  int64_t numsyms = bfd_canonicalize_symtab(bfd, symtab);

  if (numsyms < 0) {
    fprintf(stderr, "[BFD]: %s\n", bfd_errmsg(errno));
    return -1;
  }

  for (uint64_t i = 0; i < numsyms; i++) {
    emit_sym(symtab[i]);
  }

  bfd_cleanup(bfd);
}

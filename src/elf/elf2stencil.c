#include "core.h"
#include "platform.h"
#include <elf.h>
#include <libelf.h>
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

void emit_scn(Elf *elf, size_t section, Elf_Scn *strtab, Elf_Scn *symtab) {
  Elf64_Ehdr *ehdr = elf64_getehdr(elf);

  size_t stridx = elf_ndxscn(strtab);
  Elf_Data *symdata = elf_getdata(symtab, nullptr);

  if (symdata->d_type != ELF_T_SYM) {
    fprintf(stderr, "Symbol table had non-symbol type %i\n", symdata->d_type);
    return;
  }

  Elf_Scn *scn = elf_getscn(elf, section);
  Elf_Scn *scn_rela = elf_getscn(elf, section + 1);

  Elf64_Shdr *scn_hdr = elf64_getshdr(scn);
  Elf64_Shdr *scn_rela_hdr = elf64_getshdr(scn_rela);

  Elf_Data *scn_data = elf_getdata(scn, nullptr);
  Elf_Data *scn_rela_data = elf_getdata(scn_rela, nullptr);

  if (scn_data->d_type != ELF_T_BYTE) {
    fprintf(stderr, "%s had non-byte type %i\n",
            elf_strptr(elf, stridx, scn_hdr->sh_name), scn_data->d_type);
    return;
  }

  const char *scn_name = elf_strptr(elf, stridx, scn_hdr->sh_name);
  size_t scn_name_len = strlen(scn_name);

  // Header for generated stencil
  printf("\n/* EMIT %.*s */\n\n", (int)scn_name_len - 6, scn_name + 6);

  // Stencil's code.
  printf("uint8_t %.*s_BYTES[] = {\n\t", (int)scn_name_len - 6, scn_name + 6);
  size_t bytes_len = scn_data->d_size;

  for (size_t i = 0; i < bytes_len; i++) {
    unsigned char byte = ((unsigned char *)scn_data->d_buf)[i];
    printf("0x%02x, ", byte);
  }
  printf("\n};\n");

  // Stencil's rela relocations.
  if (scn_rela_data->d_type != ELF_T_RELA) {
    fprintf(stderr, "%s had non-rela type %i. Emitting empty relocations.\n",
            elf_strptr(elf, stridx, scn_rela_hdr->sh_name), scn_data->d_type);
    printf("struct gab_jit_reloc %.*s_RELAS[] = {};\n", (int)scn_name_len - 6,
           scn_name + 6);
    return;
  }

  size_t nrela = scn_rela_data->d_size / sizeof(Elf64_Rela);
  printf("struct gab_jit_reloc %.*s_RELAS[] = {\n", (int)scn_name_len - 6,
         scn_name + 6);
  for (size_t i = 0; i < nrela; i++) {
    // Look up this relocation in our rela buf.
    Elf64_Rela rela = ((Elf64_Rela *)scn_rela_data->d_buf)[i];

    if (rela.r_offset >= bytes_len)
      continue;

    // Look up the symbol we are relocating in the symbol table.
    Elf64_Sym sym = ((Elf64_Sym *)symdata->d_buf)[ELF64_R_SYM(rela.r_info)];

    const char *symname = elf_strptr(elf, stridx, sym.st_name);

    if (sym.st_shndx == SHN_UNDEF) {
      // TODO: Check the symbol for known holes. We can only patch those we
      // know!
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

      Elf_Scn *scn = elf_getscn(elf, sym.st_shndx);

      Elf_Data *data = elf_getdata(scn, nullptr);
      const unsigned char *src = (unsigned char *)data->d_buf + sym.st_value;

      size_t len = sym.st_size ? sym.st_size : data->d_align;

      printf("\t{ kGAB_JIT_RELOC_CONST, %lu, %li, ", rela.r_offset,
             rela.r_addend);
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
    fprintf(stderr, "stdin is not a file.\n");
    return -1;
  }

  elf_version(EV_CURRENT);

  Elf *elf = elf_begin(gab_osfileno(stdin), ELF_C_READ, nullptr);

  if (!elf) {
    fprintf(stderr, "[ELF]: %s\n", elf_errmsg(errno));
    return -1;
  }

  if (elf_kind(elf) != ELF_K_ELF) {
    fprintf(stderr, "[ELF]: Not elf file.\n");
    return -1;
  }

  size_t n = 0;
  elf_getshdrnum(elf, &n);

  size_t scn_str = 0;
  elf_getshdrstrndx(elf, &scn_str);

  Elf_Scn *strtab = elf_getscn(elf, scn_str);
  assert(elf64_getshdr(strtab)->sh_type == SHT_STRTAB);

  Elf_Scn *symtab = elf_getscn(elf, n - 1);
  assert(elf64_getshdr(symtab)->sh_type == SHT_SYMTAB);

  for (size_t section = 0; section < n; section++) {
    Elf_Scn *scn = elf_getscn(elf, section);

    Elf64_Shdr *scn_hdr = elf64_getshdr(scn);

    const char *scn_name = elf_strptr(elf, scn_str, scn_hdr->sh_name);

    if (scn_isop(scn_name))
      emit_scn(elf, section, strtab, symtab);
  }

  elf_end(elf);
}

/*
** Copyright (C) 2011 EADS France
** stephane duverger <stephane.duverger@eads.net>
** nicolas bareil <nicolas.bareil@eads.net>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along
** with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <stdio.h>
#include <elf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>

#define LDOUTFILE  "ld-hook.so"

extern void shatner(void);
extern void payload(void);

/*
** File-to-load constraints:
**  - pie
**  - no global variables (.data/.bss)
**  - no external symbols (.reloc*, .got.plt entries)
**
** $ gcc -fpie -c file.c -o file.o
** $ ld -pie -e your_function file.o -o file
*/
int load_code_from_file(char *fname, Elf32_Ehdr **fe, uint32_t *csz)
{
   Elf32_Ehdr	*e_hdr;
   Elf32_Phdr   *p_hdr;
   int		fd, i;
   off_t	fd_sz;

   if ((fd = open(fname, O_RDONLY)) == -1)
   {
      printf("can't open file %s\n", fname);
      return -1;
   }

   fd_sz = lseek(fd, 0, SEEK_END);
   lseek(fd, 0, SEEK_SET);
   e_hdr = (Elf32_Ehdr*)mmap(NULL, fd_sz, PROT_READ, MAP_PRIVATE, fd, 0);
   p_hdr = (Elf32_Phdr*)((uint32_t)e_hdr + e_hdr->e_phoff);

   *fe = e_hdr;
   *csz = 0;
   for (i=0 ; i<e_hdr->e_phnum ; i++, p_hdr++)
      if (p_hdr->p_type == PT_LOAD)
	 *csz += p_hdr->p_memsz;

   return 0;
}

void inject_code_from_file(uint32_t base, uint32_t load, Elf32_Ehdr *e_hdr)
{
   Elf32_Phdr   *p_hdr;
   uint32_t     *fix, offset, diff;
   int		i;

   p_hdr = (Elf32_Phdr*)((uint32_t)e_hdr + e_hdr->e_phoff);

   printf("---> injecting code\n");

   offset = load;
   for (i=0 ; i<e_hdr->e_phnum ; i++, p_hdr++)
   {
      if (p_hdr->p_type == PT_LOAD)
      {
   	 uint32_t loc = (uint32_t)e_hdr + p_hdr->p_offset;
   	 memcpy((void*)offset, (void*)loc, p_hdr->p_filesz);
	 offset += p_hdr->p_filesz;

	 diff = p_hdr->p_memsz - p_hdr->p_filesz;
	 if (diff)
	 {
	    memset((void*)offset, 0, diff);
	    offset += diff;
	 }
      }
   }

   /* give entry point */
   fix  = (uint32_t*)((uint32_t)base + 10);
   *fix = e_hdr->e_entry;
}

/*
** check if section holding that reloc entry
** has file offset different from load addr
** in which case we may access out of file' size
*/
uint32_t reloc_lookup_section(Elf32_Ehdr *e_hdr, uint32_t offset)
{
   int        i;
   Elf32_Shdr *s_hdr = (Elf32_Shdr*)((uint32_t)e_hdr + e_hdr->e_shoff);

   for (i=0 ; i<e_hdr->e_shnum ; i++, s_hdr++)
      if ((s_hdr->sh_type == SHT_PROGBITS) && (s_hdr->sh_flags & SHF_ALLOC))
	 if (offset >= s_hdr->sh_addr && offset < (s_hdr->sh_addr+s_hdr->sh_size))
	    return (s_hdr->sh_addr - s_hdr->sh_offset);

   return 0;
}

int main(int argc, char **argv)
{
   Elf32_Ehdr	*e_hdr_in, *e_hdr_out, *e_hdr_inj;
   Elf32_Phdr	*p_hdr_in, *p_hdr_out, *code_hdr, *data_hdr, *dyn_hdr;
   Elf32_Shdr   *s_hdr_out, *s_hdr_str_out;
   int		fd_in;
   int		fd_out;
   off_t	fd_in_sz, fd_out_sz;
   uint32_t     pltgot, *fix, msk, user_offset, base_block, hook_sz, inj_sz, over_sz;
   char         *s_str_out;
   int          i;

   if (argc != 3)
   {
      printf("usage: %s <ld-linux.so> <pie.elf>\n", argv[0]);
      return -1;
   }

   /* check input file */
   if ((fd_in = open(argv[1], O_RDONLY)) == -1)
   {
      printf("can't open file %s\n", argv[1]);
      return -1;
   }

   fd_in_sz = lseek(fd_in, 0, SEEK_END);
   lseek(fd_in, 0, SEEK_SET);
   e_hdr_in = (Elf32_Ehdr*)mmap(NULL, fd_in_sz, PROT_READ, MAP_PRIVATE, fd_in, 0);
   p_hdr_in = (Elf32_Phdr*)((uint32_t)e_hdr_in + e_hdr_in->e_phoff);

   /* load file to inject */
   if (load_code_from_file(argv[2], &e_hdr_inj, &inj_sz) == -1)
      return -1;

   hook_sz = (uint32_t)payload - (uint32_t)shatner;
   over_sz = hook_sz + inj_sz;

   /* build out file */
   if ((fd_out = open(LDOUTFILE, O_RDWR|O_CREAT|O_TRUNC, 0755)) == -1)
   {
      printf("can't open file "LDOUTFILE"\n");
      return -1;
   }

   fd_out_sz = fd_in_sz + over_sz;

   lseek(fd_out, fd_out_sz - 1, SEEK_SET);
   write(fd_out, "\x00", 1);
   lseek(fd_out, 0, SEEK_SET);
   e_hdr_out = (Elf32_Ehdr*)mmap(NULL, fd_out_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd_out, 0);

   /* copy elf hdr */
   uint32_t offset = (uint32_t)e_hdr_out;
   memcpy((void*)offset, (void*)e_hdr_in, e_hdr_in->e_ehsize);

   /* install hook */
   offset += e_hdr_in->e_ehsize;
   memcpy((void*)offset, (void*)shatner, hook_sz);

   base_block = offset;

   /* save fixed ld entry point */
   fix = (uint32_t*)((uint32_t)base_block + 2);
   *fix = e_hdr_out->e_entry + over_sz;

   /* inject external code */
   inject_code_from_file(base_block, offset+hook_sz, e_hdr_inj);

   /* copy remaining */
   offset += over_sz;
   memcpy((void*)offset, (void*)e_hdr_in+e_hdr_in->e_ehsize, fd_in_sz - e_hdr_in->e_ehsize);

   /*
   ** Fix Elf header
   */
   e_hdr_out->e_entry  = e_hdr_out->e_ehsize;
   e_hdr_out->e_phoff += over_sz;
   e_hdr_out->e_shoff += over_sz;

   /*
   ** Fix Program headers
   */
   printf("p_hdr #%d\n", e_hdr_out->e_phnum);
   p_hdr_out = (Elf32_Phdr*)((uint32_t)e_hdr_out + e_hdr_out->e_phoff);
   code_hdr = (Elf32_Phdr*)0;
   data_hdr = (Elf32_Phdr*)0;

   for (i=0 ; i<e_hdr_out->e_phnum ; i++, p_hdr_out++)
   {
      if (!p_hdr_out->p_memsz)
	 continue;

      if (p_hdr_out->p_type == PT_LOAD && (p_hdr_out->p_flags & (PF_R|PF_X)) && i == 0)
      {
	 code_hdr = p_hdr_out;
	 code_hdr->p_filesz += over_sz;
	 code_hdr->p_memsz  += over_sz;
	 continue;
      }

      if (p_hdr_out->p_type == PT_LOAD && (p_hdr_out->p_flags & (PF_R|PF_W)) && i == 1)
      {
	 data_hdr = p_hdr_out;
	 msk = data_hdr->p_align - 1;
      }

      if (p_hdr_out->p_type == PT_DYNAMIC)
	 dyn_hdr = p_hdr_out;

      printf("fixing phdr %d\n", i);
      p_hdr_out->p_offset += over_sz;
      p_hdr_out->p_vaddr  += over_sz;
      p_hdr_out->p_paddr  += over_sz;
   }

   if (!code_hdr || !data_hdr || !dyn_hdr)
   {
      printf("bad program headers\n");
      return -1;
   }

   /*
   ** Fix Section headers
   */
   s_hdr_out = (Elf32_Shdr*)((uint32_t)e_hdr_out + e_hdr_out->e_shoff);
   s_hdr_str_out = &s_hdr_out[e_hdr_out->e_shstrndx];
   s_str_out = (uint8_t*)e_hdr_out + s_hdr_str_out->sh_offset + over_sz;
   printf("s_hdr #%d | s_hdr_str 0x%x\n", e_hdr_out->e_shnum, s_str_out);

   for (i=0 ; i<e_hdr_out->e_shnum ; i++, s_hdr_out++)
   {
      if (s_hdr_out->sh_size == 0)
	 continue;

      s_hdr_out->sh_offset += over_sz;

      if (s_hdr_out->sh_flags & SHF_ALLOC)
	 s_hdr_out->sh_addr += over_sz;

      printf("\\_fix shdr %s addr 0x%x off 0x%x\n",
	     &s_str_out[s_hdr_out->sh_name], s_hdr_out->sh_addr, s_hdr_out->sh_offset);

      /* fix .dynamic */
      if (s_hdr_out->sh_type == SHT_DYNAMIC)
      {
	 Elf32_Dyn *dyn_s = (Elf32_Dyn*)((uint32_t)e_hdr_out + s_hdr_out->sh_offset);
	 Elf32_Dyn *dyn   = dyn_s;

	 while (dyn->d_tag != DT_NULL)
	 {
	    switch (dyn->d_tag)
	    {
	    case DT_PLTGOT:
	    case DT_JMPREL:
	    case DT_GNU_HASH:
	    case DT_HASH:
	    case DT_STRTAB:
	    case DT_SYMTAB:
	    case DT_REL:
	    case DT_RELA:
	    case DT_VERDEF:
	    case DT_VERSYM:
	       dyn->d_un.d_ptr += over_sz;
	       printf(" \\_fix 0x%x\n", dyn->d_un.d_ptr);
	       break;
	    case DT_SONAME:
	       printf(" \\_soname 0x%x\n", dyn->d_un.d_val);
	       break;
	    case DT_INIT_ARRAY:
	    case DT_FINI_ARRAY:
	       printf("!!!! WARNING need to patch ARRAY !!!!\n");
	       break;
	    }

	    if (dyn->d_tag == DT_PLTGOT)
	    {
	       pltgot = dyn->d_un.d_ptr;
	       printf(" \\_found .got.plt @ 0x%x\n", pltgot);
	    }

	    dyn++;
	 }

	 /* compute user offset to save old user entry into .dynamic[NULL].d_un.d_ptr */
	 user_offset = ((code_hdr->p_memsz + msk) & (~msk)) + (dyn_hdr->p_vaddr & msk) + ((uint32_t)dyn - (uint32_t)dyn_s) + 4;
	 printf("computed user_offset 0x%x\n", user_offset);

	 fix  = (uint32_t*)((uint32_t)base_block + 6);
	 *fix = user_offset;
      }

      /* fix .dynsym */
      if (s_hdr_out->sh_type == SHT_DYNSYM)
      {
      	 Elf32_Sym *sym = (Elf32_Sym*)((uint32_t)e_hdr_out + s_hdr_out->sh_offset);
      	 uint32_t nr = s_hdr_out->sh_size/s_hdr_out->sh_entsize;
      	 int z;
      	 for (z=0 ; z<nr ; z++)
      	 {
	    if (sym->st_size)
	    {
	       sym->st_value += over_sz;
	       printf(" \\_fix 0x%x\n", sym->st_value);
	    }
      	    sym++;
      	 }
      }

      /* fix .got.plt */
      if (s_hdr_out->sh_addr == pltgot)
      {
      	 uint32_t *got = (uint32_t*)((uint32_t)e_hdr_out + s_hdr_out->sh_offset);
      	 uint32_t nr = s_hdr_out->sh_size/s_hdr_out->sh_entsize;
      	 int z;
      	 for (z=0 ; z<nr ; z++)
      	 {
	    *got += over_sz;
	    printf(" \\_fix 0x%x\n", *got);
      	    got++;
      	 }
      }
   }

   /*
   ** now that s_hdr are fixed, process reloc entries
   */
   s_hdr_out = (Elf32_Shdr*)((uint32_t)e_hdr_out + e_hdr_out->e_shoff);
   for (i=0 ; i<e_hdr_out->e_shnum ; i++, s_hdr_out++)
   {
      if (s_hdr_out->sh_type != SHT_REL)
	 continue;

      printf("\\_reloc %s fix\n", &s_str_out[s_hdr_out->sh_name]);

      Elf32_Rel *rel = (Elf32_Rel*)((uint32_t)e_hdr_out + s_hdr_out->sh_offset);
      uint32_t	 nr  = s_hdr_out->sh_size/s_hdr_out->sh_entsize;
      int	 z;
      for (z=0 ; z<nr ; z++)
      {
	 rel->r_offset += over_sz;
	 printf(" \\_fix 0x%x\n", rel->r_offset);

	 if (ELF32_R_TYPE(rel->r_info) == R_386_RELATIVE)
	 {
	    uint32_t rfix = (uint32_t)e_hdr_out + rel->r_offset;
	    uint32_t diff = reloc_lookup_section(e_hdr_out, rel->r_offset);

	    if (diff)
	       rfix -= diff;

	    *(uint32_t*)rfix += over_sz;
	 }

	 rel++;
      }
   }

   munmap((void*)e_hdr_in, fd_in_sz);
   close(fd_in);
   munmap((void*)e_hdr_out, fd_out_sz);
   close(fd_out);
   return 0;
}

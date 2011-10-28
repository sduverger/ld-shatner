#include <stdio.h>
#include <elf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>

#define LDNAME "/lib/ld-hook.so"

int main(int argc, char **argv)
{
   Elf32_Ehdr	*e_hdr;
   Elf32_Phdr	*p_hdr;
   int		fd, i;
   off_t	fd_sz;
   uint32_t     len, old;

   if (argc != 2)
   {
      printf("usage: %s <elf>\n", argv[0]);
      return -1;
   }

   if ((fd = open(argv[1], O_RDWR)) == -1)
   {
      printf("can't open file %s\n", argv[1]);
      return -1;
   }

   fd_sz = lseek(fd, 0, SEEK_END);
   lseek(fd, 0, SEEK_SET);
   e_hdr = (Elf32_Ehdr*)mmap(NULL, fd_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
   p_hdr = (Elf32_Phdr*)((uint32_t)e_hdr + e_hdr->e_phoff);

   len = strlen(LDNAME)+1;

   for (i=0 ; i<e_hdr->e_phnum ; i++, p_hdr++)
      if (p_hdr->p_type == PT_INTERP)
      {
	 if (p_hdr->p_filesz < len)
	 {
	    printf("not enough space to fix interp name (%d)\n", p_hdr->p_filesz);
	    return -1;
	 }

	 old = p_hdr->p_filesz;
	 p_hdr->p_filesz = p_hdr->p_memsz = len;
	 memcpy((void*)((uint32_t)e_hdr+p_hdr->p_offset), LDNAME, len-1);
	 memset((void*)((uint32_t)e_hdr+p_hdr->p_offset+len-1), 0, old-len);

	 /* XXX: should also fix section header ... */
      }

   munmap((void*)e_hdr, fd_sz);
   close(fd);
   return 0;
}

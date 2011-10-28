int xwrite(int fd, void *buf, int count) {
        int ret;
        asm(
	   "xchg  %%esi, %%ebx  \n"
	   "xchg  %%edi, %%ecx  \n"
	   "int   $0x80         \n"
	   "xchg  %%esi, %%ebx  \n"
	   "xchg  %%edi, %%ecx  \n"
	   :"=a"(ret)
	   :"a"(4),"S"(fd),"D"(buf),"d"(count)
	   :"cc", "memory");
        return ret;
}

int f1(int var)
{
	return var*2;
}

int func()
{
	f1(2);
	xwrite(1,"ld-hook\n", 8);
	return 1;
}


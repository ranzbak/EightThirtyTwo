/* Hardware registers for a supporting UART to the ZPUFlex project. */

#include "uart.h"

//int thread2main(int argc,char **argv)
//{
//}

struct mystruct
{
	int v1,v2;	// 0 - 7
	unsigned int v3,v4;	// 8 - 15
	char	c1,c2;	// 16-17 (padded?)
	unsigned char	c3,c4;	// 16-17 (padded?)
	short	s1,s2;	// 18-21
	unsigned short s3,s4;	// 22-25
	char *end;	// 26?
};

struct mystruct st=
{
	0xc0000000,0x1000,
	0xffff0000,0x0100,
	0x9c,0x49,
	0x9c,0x49,
	0x1234,0xfedc,
	0x1234,0xfedc
};


int main(int argc,char **argv)
{
	st.end=".\n";
	if(st.v1<st.v2) putchar('0'); else putchar('A');
	if(st.v3<st.v4)	putchar('1'); else putchar('B');
	if(st.v1<st.v3)	putchar('2'); else putchar('C');
	if(st.v1<st.c1)	putchar('3'); else putchar('D');
	if(st.c1<st.c2) putchar('4'); else putchar('E');
	if(st.c3<st.c4) putchar('5'); else putchar('F');
	if(st.s1<st.s2) putchar('6'); else putchar('G');
	if(st.s3<st.s4) putchar('7'); else putchar('H');

	st.c1=0x1c;
	if(st.c1<st.c2) putchar('4'); else putchar('E');
	

	puts(st.end);
	return(-1);
}

int putchar(int c)
{
	volatile int *uart=&HW_UART(REG_UART);
	do {} while(!((*uart)&(1<<REG_UART_TXREADY)));

	*uart=c;
	return(c);
}


int puts(const char *msg)
{
	int c;
	int result=0;
	int *s2=(int*)msg;

	while(1)
	{
		int i;
		int cs=*s2++;
		for(i=0;i<4;++i)
		{
			c=cs&0xff;
			cs>>=8;
			if(c==0)
				return(result);
			putchar(c);
			++result;
		}
	}
	return(result);
}

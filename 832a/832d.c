/* Disassembler for 832 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "832a.h"

int main(int argc,char **argv)
{
	FILE *f;
	if(argc>1)
	{
		int i;
		for(i=1;i<argc;++i)
		{
			f=fopen(argv[i],"rb");
			if(f)
			{
				int a=0;
				int c;
				int imm;
				int signedimm;
				int immstreak=0;
				while((c=fgetc(f))!=EOF)
				{
					int j;
					if((c&0xc0)==0xc0)
					{
						if(immstreak)
							imm<<=6;
						else
							imm=c&0x20 ? 0xffffffc0 : 0;
						imm|=c&0x3f;
						if(imm&0x80000000)
						{
							signedimm=~imm;
							signedimm+=1;
							signedimm=-signedimm;
						}
						else
							signedimm=imm;
						printf("%05x\t%02x\tim\t%x\t(0x%x, %d)\n",a,c,c&0x3f,imm,signedimm);
						immstreak=1;
					}
					else
					{
						int found=0;
						immstreak=0;
						/* Look for overloads first... */
						if((c&7)==7)
						{
							for(j=0;j<sizeof(opcodes)/sizeof(struct opcode);++j)
							{
								if(opcodes[j].opcode==(c))
								{
									found=1;
									printf("%05x\t%02x\t%s\n",a,c,opcodes[j].mnem);
									break;
								}
							}
						}
						if(!found)
						{
							/* If not found, look for base opcodes... */
							for(j=0;j<sizeof(opcodes)/sizeof(struct opcode);++j)
							{
								if(opcodes[j].opcode==(c&0xf8))
								{
									printf("%05x\t%02x\t%s\t%s\n",a,c,opcodes[j].mnem,operands[(c&7)|((c&0xf8)==0 ? 8 : 0)].mnem);
									break;
								}
							}
						}
					}
					++a;
				}		
			}
			fclose(f);
		}
	}
	return(0);
}

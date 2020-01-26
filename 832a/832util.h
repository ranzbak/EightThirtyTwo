#ifndef UTIL832_H
#define UTIL832_H

void error_setfile(const char *fn);
void error_setline(int line);
void asmerror(const char *err);

void write_int_le(int i,FILE *f);
void write_short_le(int i,FILE *f);
void write_lstr(const char *str,FILE *f);

#endif

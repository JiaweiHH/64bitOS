#ifndef _LINKAGE_H_
#define _LINKAGE_H_

/*

*/

#define L1_CACHE_BYTES 32

#define asmlinkage __attribute__((regparm(0)))

#define ____cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))

#define SYMBOL_NAME(X) X

/* 预处理符号 # 强制将其后内容转化为字符串 */
#define SYMBOL_NAME_STR(X) #X

/* 符号 ## 用于字符串连接 */
#define SYMBOL_NAME_LABEL(X) X##:

/*

*/

#define ENTRY(name)                                                            \
  .global SYMBOL_NAME(name);                                                   \
  SYMBOL_NAME_LABEL(name)

#endif

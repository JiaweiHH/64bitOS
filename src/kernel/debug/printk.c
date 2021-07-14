#include <stdarg.h>
#include "printk.h"
#include "lib.h"
#include "linkage.h"

extern inline int strlen(char *String);

/**
 * 将整数值按照指定进制规格转换成字符串
 * @str: 待显示字符串缓冲区
 * @num: 带转换的整数
 * @base: 指定的进制
 * @precision: 精度，@size: 位宽，@type: 标志位
 */
static char *number(char *str, long num, int base, int size, int precision,
                    int type) {
  char c, sign, tmp[50];
  /* 待显示的进制字符串 */
  const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int i;

  if (type & SMALL)   /* 小写字符 */
    digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (type & LEFT)    /* 左对齐就不需要 0 填充了 */
    type &= ~ZEROPAD;
  if (base < 2 || base > 36)  /* 进制超出范围直接返回 nullptr */
    return 0;

  /* 如果不需要 0 填充就用空格填充 */
  c = (type & ZEROPAD) ? '0' : ' ';

  sign = 0;
  if (type & SIGN && num < 0) {   /* 如果是有符号数并且带转换的是负数，记录 '-' */
    sign = '-';
    num = -num;
  } else {
    /**
     * 判断对于正数需不需要显示 '+'
     * 如果不需要使用 '+'，继续判断需不需要用 ' ' 占位
     */
    sign = (type & PLUS) ? '+' : ((type & SPACE) ? ' ' : 0);
  }
  if (sign) /* 如果 sign 占了一个字符，位宽 -1 */
    --size;
  if (type & SPECIAL)     /* 有特殊字符？ */
    if (base == 16)       /* 需要显示 0x 位宽 -2 */
      size -= 2;
    else if (base == 8)   /* 8 进制只显示一个 '0' */
      --size;

  /**
   * 下面根据 num 的数值转换成对应的进制字符保存到 tmp 中，
   * i 记录了转换后的字符串长度
   */
  i = 0;
  if (num == 0)
    tmp[i++] = '0';
  else
    while (num != 0)
      tmp[i++] = digits[do_div(num, base)];

  /**
   * 如果转换后字符串长度不足 precision，则显示 precision 个进制字符
   * 如果超出了 precision，则正常显示
   * 然后根据需要显示的进制字符个数，更新剩余位宽
   */  
  if (i > precision)
    precision = i;
  size -= precision;

  /* 既没有零填充也没有左对齐，则在显示进制字符前使用空格补充 */
  if (!(type & (ZEROPAD + LEFT)))
    while (size-- > 0)
      *str++ = ' ';
  /**
   * 根据前面的判断是不是需要显示符号
   * 前面已经把 size-- 了
   */
  if (sign)
    *str++ = sign;
  
  /**
   * 根据前面的判断是不是需要显示特殊字符
   * 前面已经更新过 size 了
   */
  if (type & SPECIAL)
    if (base == 8)          /* 显示八进制的 0 */
      *str++ = '0';
    else if (base == 16) {  /* 显示十六进制的 0x */
      *str++ = '0';
      *str++ = digits[33];
    }
  
  /**
   * 在这里说明 if (!(type & (ZEROPAD + LEFT))) 判断没有执行
   * 继续判断是不是因为不需要左对齐而没有执行上述判断，
   * 如果是的话根据前面的 c 来补充进制字符前面的字符，c 是根据是不是要零填充得到的一个字符
   */
  if (!(type & LEFT))
    while (size-- > 0)
      *str++ = c;

  /**
   * 进制字符串的长度 < 显示精度
   * 使用 '0' 补充不足的部分 
   */
  while (i < precision--)
    *str++ = '0';

  /* 保存进制字符串 */
  while (i-- > 0)
    *str++ = tmp[i];

  /* 在这里说明需要左对齐，那么使用 ' ' 补充剩余字符 */
  while (size-- > 0)
    *str++ = ' ';
  
  /* 返回待显示字符的缓冲区 */
  return str;
}

/* 将字符串数值转换成整数值 */
int skip_atoi(const char **s) {
  int i = 0;
  while (is_digit(**s)) {
    i = i * 10 + *((*s)++) - '0';
  }
  return i;
}

int vsprintf(char *buf, const char *fmt, va_list args) {
  char *str, *s;
  int flags;
  int field_width;
  int precision;
  int len, i;

  int qualifier; /* 'h', 'l', 'L' or 'Z' for integer fields */

  for (str = buf; *fmt; fmt++) {
    if (*fmt != '%') {  /* 如果是可显示字符，直接存入缓冲区 */
      *str++ = *fmt;
      continue;
    }
    flags = 0;
  repeat:
    fmt++;  // 跳过 '%'
    switch (*fmt) {
    case '-':
      flags |= LEFT;
      goto repeat;
    case '+':
      flags |= PLUS;
      goto repeat;
    case ' ':
      flags |= SPACE;
      goto repeat;
    case '#':
      flags |= SPECIAL;
      goto repeat;
    case '0':
      flags |= ZEROPAD;
      goto repeat;
    }

    /* 获取数据区域的宽度 */
    field_width = -1;
    if (is_digit(*fmt))
      field_width = skip_atoi(&fmt);
    else if (*fmt == '*') {
      /* 如果下一个字符不是数字而是 '*'，那么数据区域的宽度将由可变参数提供 */
      fmt++;
      field_width = va_arg(args, int);
      if (field_width < 0) {
        field_width = -field_width;
        flags |= LEFT;
      }
    }

    /**
     * 获取精度
     * 如果数据区域宽度后面跟有字符 '.'，说明其后的数值是显示数据的精度
     */
    precision = -1;
    if (*fmt == '.') {
      fmt++;
      if (is_digit(*fmt))
        precision = skip_atoi(&fmt);
      else if (*fmt == '*') {
        /* 如果是 '*'，精度由可变参数提供 */
        fmt++;
        precision = va_arg(args, int);
      }
      if (precision < 0)
        precision = 0;
    }

    /* 检测数据的规格，例如 "%ld" 中的 'l' 表示长整型 */
    qualifier = -1;
    if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' || *fmt == 'Z') {
      qualifier = *fmt;
      fmt++;
    }

    /* 到达这里数据区域的宽度和精度信息都已经获取 */

    /* 下面进入可变参数的字符串格式化转换过程 */
    switch (*fmt) {
    case 'c':

      if (!(flags & LEFT))
        while (--field_width > 0)
          *str++ = ' ';
      *str++ = (unsigned char)va_arg(args, int);
      while (--field_width > 0)
        *str++ = ' ';
      break;

    case 's':

      s = va_arg(args, char *);
      if (!s)
        s = '\0';
      len = strlen(s);
      if (precision < 0)
        precision = len;
      else if (len > precision)
        len = precision;

      if (!(flags & LEFT))
        while (len < field_width--)
          *str++ = ' ';
      for (i = 0; i < len; i++)
        *str++ = *s++;
      while (len < field_width--)
        *str++ = ' ';
      break;

    case 'o':

      if (qualifier == 'l')
        str = number(str, va_arg(args, unsigned long), 8, field_width,
                     precision, flags);
      else
        str = number(str, va_arg(args, unsigned int), 8, field_width, precision,
                     flags);
      break;

    case 'p':

      if (field_width == -1) {
        field_width = 2 * sizeof(void *);
        flags |= ZEROPAD;
      }

      str = number(str, (unsigned long)va_arg(args, void *), 16, field_width,
                   precision, flags);
      break;

    case 'x':

      flags |= SMALL;

    case 'X':

      if (qualifier == 'l')
        str = number(str, va_arg(args, unsigned long), 16, field_width,
                     precision, flags);
      else
        str = number(str, va_arg(args, unsigned int), 16, field_width,
                     precision, flags);
      break;

    case 'd':
    case 'i':

      flags |= SIGN;
    case 'u':

      if (qualifier == 'l')
        str = number(str, va_arg(args, unsigned long), 10, field_width,
                     precision, flags);
      else
        str = number(str, va_arg(args, unsigned int), 10, field_width,
                     precision, flags);
      break;

    case 'n':

      if (qualifier == 'l') {
        long *ip = va_arg(args, long *);
        *ip = (str - buf);
      } else {
        int *ip = va_arg(args, int *);
        *ip = (str - buf);
      }
      break;

    case '%':

      *str++ = '%';
      break;

    default:

      *str++ = '%';
      if (*fmt)
        *str++ = *fmt;
      else
        fmt--;
      break;
    }
  }
  *str = '\0';
  return str - buf;   /* 返回 buf 的长度 */
}

/**
 * @font: 待显示字符的 ASCII 编号
 * 1. 通过 font 获取预先设置好的 ASCII 矩阵信息
 * 2. 检查该矩阵的每一个 bit，如果是 1 表示该像素显示，否则不显示
 * 3. 针对像素是否显示，在帧缓存区设置像素的颜色信息
 * 一个字符的宽度是 16 行 8 列
 */
void putchar(unsigned int *fb, int Xsize, int x, int y, unsigned int FRcolor,
             unsigned int BKcolor, unsigned char font) {
  int i = 0, j = 0;
  unsigned int *addr = NULL;
  unsigned char *fontp = NULL;
  int testval = 0;
  /**
   * 获取 font 字符对应的 [ASCII 矩阵]，根据 [ASCII 矩阵] 来设置帧缓存区；
   * 每个字符对应 8*16 的 bit 矩阵，总共是 16B
   */
  fontp = font_ascii[font];

  /**
   * 由于 [ASCII 矩阵] 总共有 16 行，所以需要循环 16 次；
   * 1. 外层循环，每次循环完成设置一行所有像素点的颜色深度
   * 2. 内存循环，每次循环完成设置一个像素点的颜色深度
   */
  for(i = 0; i < 16; ++i) {
    /* 帧缓存区上第 i 行初始地址 */
    addr = fb + Xsize * (y + i) + x;
    testval = 0x100;

    for(j = 0; j < 8; ++j) {
      /**
       * 第 j 次循环 testval 从左往右的第 j-bit 是 1
       * 用 testval 检查该像素在 [ASCII 矩阵] 中的数据，以此决定在帧缓存区此像素的颜色信息
       * 如果是 1 就帧缓存区像素就显示字体颜色，如果是 0 就显示背景色
       */
      testval >>= 1;
      if(*fontp & testval)  /* 如果 bit = 1 */
        *addr = FRcolor;
      else                  /* 如果 bit = 0 */
        *addr = BKcolor;
      /* 下一个像素 */
      ++addr;
    }
    /* [ASCII 矩阵] 上的下一行信息 */
    ++fontp;
  }
}

/**
 * 格式化字符串显示
 * 1. 调用 vsprintf 解析格式化字符串，将最终需要显示的内容保存到 buf
 * 2. 调用 putchar 通过设置像素点颜色在显示器上显示字符
 */
int color_printk(unsigned int FRcolor, unsigned int BKcolor, const char *fmt,
                 ...) {
  int i = 0, count = 0, line = 0;
  va_list args;
  va_start(args, fmt);
  i = vsprintf(buf, fmt, args);
  va_end(args);

  /* 检测格式化后的字符串 */
  for (count = 0; count < i || line; ++count) {
    if (line > 0) {
      --count;
      goto Label_tab;
    }
    /* 如果待显示的字符是 '\n'（换行），则将光标行数 +1，列数设置为 0 */
    if ((unsigned char)*(buf + count) == '\n') {
      ++Pos.YPosition;
      Pos.XPosition = 0;
    } else if ((unsigned char)*(buf + count) == '\b') {
      /**
       * 如果待显示的字符是 '\b'（退格），那么调整列位置并用空格覆盖之前位置的字符
       * 注意光标总是在最后一个字符的后面
       */
      --Pos.XPosition;
      if (Pos.XPosition < 0) {  /* 该行被删除完了 */
        Pos.XPosition = Pos.XResolution / Pos.XCharSize - 1;
        --Pos.YPosition;
        if (Pos.YPosition < 0) {
          Pos.YPosition = Pos.YResolution / Pos.YCharSize - 1;
        }
      }
      /* 将待删除的位置使用空格填充 */
      putchar(Pos.FB_addr, Pos.XResolution, Pos.XPosition * Pos.XCharSize,
              Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, ' ');
    } else if ((unsigned char)*(buf + count) == '\t') {
      /**
       * 如果待显示的字符是 '\t'
       * 1. 计算当前光标距离下一个制表位所需要填充的空格数量
       * 2. 将显示位置移到下一个制表位，并使用空格填补调整过程中占用的字符显示空间
       */
      /* 计算距离下一个制表位的距离 */
      line = ((Pos.XPosition + 8) & ~(8 - 1)) - Pos.XPosition;
    Label_tab:
      --line;
      /**
       * 显示一个字符，后续如果 line 还是大于 0，说明还没有填充满；
       * 则会继续通过 for 循环以及前面的 if(line > 0) 条件判断再次跳转到这里进行填充
       */
      putchar(Pos.FB_addr, Pos.XResolution, Pos.XPosition * Pos.XCharSize,
              Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor, ' ');
      ++Pos.XPosition;
    } else {
      /* 到达这里说明待显示就是普通字符 */
      putchar(Pos.FB_addr, Pos.XResolution, Pos.XPosition * Pos.XCharSize,
              Pos.YPosition * Pos.YCharSize, FRcolor, BKcolor,
              (unsigned char)*(buf + count));
      ++Pos.XPosition;
    }

    /* 显示完一个字符判断一下光标是不是超出最大行或者最大列了 */
    if(Pos.XPosition >= Pos.XResolution / Pos.XCharSize) {
      Pos.XPosition = 0;
      ++Pos.YPosition;
    }
    if(Pos.YPosition >= Pos.YResolution / Pos.YCharSize) {
      Pos.YPosition = 0;
    }
  }
  return i;
}
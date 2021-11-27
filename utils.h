#ifndef UTILS_H
#define UTILS_H
#include <syslog.h>

//******** TERMINAL COLOR
#define KRED	"\033[31m"
#define KGRN   	"\033[32m"
#define KYEL   	"\033[33m"
#define KBLU   	"\033[34m"
#define KMAG   	"\033[35m"
#define KCYN   	"\033[36m"
#define KWHT   	"\033[37m"
#define RESET	"\033[0m"

#define DISPLAYLOG	1
#define SYSLOG	0
#define DEBUG



#ifdef DEBUG
#define _DEBUG	1
#else
#define _DEBUG	0
#endif

/*
 * Macro Name: debug_cond / debug
 * Input:
 * Outup:
 */
#define debug(fmt, args...)				debug_cond(_DEBUG, fmt, ##args)
#define debug_cond(cond, fmt, args...)												\
    do {																			\
        if (cond) {																		\
            if(DISPLAYLOG){															\
                fprintf( stdout, "%s[DEBUG]:%s ", KBLU, RESET );					\
                fprintf( stdout, "%sfunction: %s %s-->\t", KCYN, __func__, RESET );	\
                fprintf( stdout, fmt, ##args);										\
            }																		\
        }																			\
    } while (0)

#endif // UTILS_H

#ifndef UTILS_H
#define UTILS_H
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
/*
 * CONFIGS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#define SRC_MAC "00:00:00:00:00:00"

#define ATT_OP_HANDLE_NOTIFY		0x1B
#if defined(__x86_64__)
#define CONFIG_FILE "./example.cfg"
#else
#define CONFIG_FILE "/etc/sossaitherm/sossaitherm.cfg"
#endif

#define ATT_OP_HANDLE_NOTIFY           0x1B


//******** TERMINAL COLOR
#define KRED	"\033[31m"
#define KGRN   	"\033[32m"
#define KYEL   	"\033[33m"
#define KBLU   	"\033[34m"
#define KMAG   	"\033[35m"
#define KCYN   	"\033[36m"
#define KWHT   	"\033[37m"
#define RESET	"\033[0m"

#define DEBUG
#define _ERROR  1


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
        if (cond) {																	\
            if(isatty (STDOUT_FILENO)) {                                            \
                fprintf( stdout, "%s[DEBUG]:%s ", KBLU, RESET );					\
                fprintf( stdout, "%sfunction: %s %s-->\t", KCYN, __func__, RESET );	\
                fprintf( stdout, fmt, ##args);										\
            } else {                                                                \
                fprintf( stdout, "[DEBUG]: " );                                     \
                fprintf( stdout, "function: %s -->\t", __func__ );                  \
                fprintf( stdout, fmt, ##args);                                      \
            }                                                                       \
        }																			\
    } while (0)                                                                     \

/*
 * Macro Name: error_cond / error
 * Input:
 * Outup:
 */
#define error(fmt, args...)				error_cond(_ERROR, fmt, ##args)
#define error_cond(cond, fmt, args...)												\
    do {																			\
        if (cond) {																	\
            if(isatty (STDOUT_FILENO)) {                                            \
                fprintf( stderr, "%s[ERROR]:%s ", KRED, RESET );					\
                fprintf( stderr, "%sfunction: %s %s-->\t", KCYN, __func__, RESET );	\
                fprintf( stderr, fmt, ##args);										\
            } else {                                                                \
                fprintf( stderr, "[ERROR]: " );                                     \
                fprintf( stderr, "function: %s -->\t", __func__ );                  \
                fprintf( stderr, fmt, ##args);                                      \
            }                                                                       \
        }																			\
    } while (0)                                                                     \

/*
 * Functions prototipes
 * * * * * * * * * * * * * * * */
uint8_t ucUtilsOpenLockFile(void);
uint8_t ucUtilsCloseUnlockFile(void);

#endif // UTILS_H

/*
 * urldecode.h
 *
 *  Created on: Aug 16, 2022
 *      Author: brauns
 */

#ifndef MAIN_URLDECODE_H_
#define MAIN_URLDECODE_H_


/*
 * Function: urlDecode
 * Purpose:  Decodes a web-encoded URL. By default, +'s are converted to spaces.
 * Input:    const char* str - the URL to decode
 * Output:   char* - the decoded URL
 */
char *urlDecode(const char *str);


#endif /* MAIN_URLDECODE_H_ */

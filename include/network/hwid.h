#ifndef __HWID_H__
#define __HWID_H__

#include <stddef.h>

const char *__hwid_get__(void);
void __network_get_client_ip__(char *out_ip, size_t max_len);

#endif /* __HWID_H__ */

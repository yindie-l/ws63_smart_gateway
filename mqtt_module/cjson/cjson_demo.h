// #ifndef CJSON_DEMO_H
// #define CJSON_DEMO_H

// char *make_json(char *service_id, char *temp, char *humidity, char *rain, char *hanger, char *pir);
// char *parse_json(char *json_string);
// char *combine_strings(int str_amount, char *str1, ...);

// #endif

#ifndef CJSON_DEMO_H
#define CJSON_DEMO_H

char *make_json(char *service_id, char *temp, char *humidity, char *rain, char *hanger, char *pir, char *light);
char *parse_json(char *json_string);
char *combine_strings(int str_amount, char *str1, ...);

int parse_command_json(char *json_string, char *target, int target_len, char *action, int action_len);
char *build_command_response(int result_code, char *status_msg);

#endif
#ifndef SPARK_H
#define SPARK_H
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include "vendor/cJSON.h"

#define SPARK_LIMIT (8u * 1024u * 1024u)
#define SPARK_TEXT 50000u
#define SPARK_MODEL_CHARACTERS 512
#define SPARK_MEMORY_ITEMS 32
#define SPARK_MEMORY_TEXT 512
#define SPARK_MEMORY_TOTAL 4096
typedef struct { char *p; size_t n, cap; } Buffer;
typedef struct { HANDLE process, job; unsigned short port; char key[65]; char *model,*chatTemplate; } Runner;
typedef int (*TextSink)(const char *, void *);
void *allocate(size_t size);
int append(Buffer *b, const char *s, size_t n);
int adds(Buffer *b, const char *s);
wchar_t *wide(const char *text);
char *utf8(const wchar_t *text);
const char *str(const cJSON *o, const char *key);
cJSON *get(const cJSON *o, const char *key);
void setstr(cJSON *o, const char *key, const char *value);
void setnum(cJSON *o, const char *key, double value);
int background_key(const wchar_t *directory, wchar_t out[100]);
int startup_path(wchar_t out[32768], char error[512]);
int startup_status(const wchar_t *linkPath, const wchar_t *directory, char error[512]);
int startup_set(const wchar_t *linkPath, const wchar_t *executable, const wchar_t *directory, int enabled, char error[512]);
void newid(char out[64]);
cJSON *new_thread(const char *name);
cJSON *new_state(void);
cJSON *import_state(const char *json, const cJSON *current, char error[512]);
cJSON *build_messages(const cJSON *state, const cJSON *thread, char error[512]);
cJSON *load_json(const wchar_t *path, char error[512]);
cJSON *parse_json(const char *text, char error[512]);
int save_json(const wchar_t *path, const cJSON *value, char error[512]);
cJSON *find_id(cJSON *array, const char *id);
int select_model(cJSON *state, const char *name, char error[512]);
int select_thread(cJSON *state, const char *id, char error[512]);
int create_conversation(cJSON *state, char error[512]);
int delete_conversation(cJSON *state, const char *id, char error[512]);
int select_character(cJSON *state, const char *id, char error[512]);
void delete_character(cJSON *state, const char *id);
int memory_option(const cJSON *state, const char *option);
cJSON *memory_notes(const cJSON *state);
char *memory_text(const cJSON *state);
int set_memories(cJSON *state, const char *text, int enabled, int automatic, char error[512]);
int remember_message(cJSON *state, const char *text, char error[512]);
typedef struct { int conversations, messages, added, conflicts, skipped; } MemoryScan;
int learn_chat_memories(cJSON *state, MemoryScan *scan, char error[512]);
typedef struct { int added, updated, messages, memories, pending, conflicts, skipped, changed; } GaboScan;
cJSON *gabo_merge(const cJSON *state, const cJSON *source, const char *origin, const cJSON *aliases, GaboScan *scan, char error[512]);
int import_gabo_journal(cJSON *state, const cJSON *source, char error[512]);
void spark_utc(char out[32]);
int activity_time_valid(const char *text);
int import_gabo_activity(cJSON *state, const cJSON *source, char error[512]);
cJSON *activity_model(cJSON *rows, const char *model);
int record_gabo_activity(cJSON *state, const cJSON *before, cJSON *rows, char error[512]);
char *gabo_activity_text(const cJSON *state, const cJSON *check, const char *model, int enabled, int checking);
int import_notifications_enabled(const cJSON *config);
int queue_import_notifications(cJSON *pending, const cJSON *event);
void import_notification_text(const cJSON *notice, wchar_t title[64], wchar_t body[256]);
cJSON *gabo_browser_snapshot(const cJSON *config, const wchar_t *scratch, HANDLE cancel, char error[512]);
cJSON *web_profile(const cJSON *state, const char *model);
cJSON *web_save_profile(const cJSON *state, const cJSON *profile, const char *model, char error[512]);
typedef struct SparkWeb SparkWeb;
typedef struct { const char *model,*route,*method; cJSON *body,*response; int status; } WebCall;
#define WM_SPARK_WEB (WM_APP+10)
SparkWeb *web_start(HWND window, const cJSON *config, const wchar_t *directory, char error[512]);
void web_stop(SparkWeb *server);
void web_open(SparkWeb *server);
cJSON *web_connection_details(SparkWeb *server);
int spark_digest(const char *a, const char *b, char out[65]);
int import_memories(cJSON *state, const cJSON *source, int required, char error[512]);
int http_request(const char *base, const wchar_t *route, const char *body, const char *key, HANDLE cancel, TextSink sink, void *context, Buffer *response, DWORD timeout, char error[512]);
int runner_start(Runner *runner, const cJSON *config, const cJSON *model, HANDLE cancel, char error[512]);
void runner_stop(Runner *runner);
int generate(Runner *runner, const cJSON *config, const cJSON *state, const cJSON *thread, HANDLE cancel, TextSink sink, void *context, char error[512]);
int generate_ex(Runner *runner,const cJSON *config,const cJSON *state,const cJSON *thread,HANDLE cancel,TextSink sink,TextSink reasoning,void *context,char error[512]);
cJSON *generation_parameters(const cJSON *body,int ollama,char error[512]);
cJSON *list_models(const cJSON *config, HANDLE cancel, char error[512]);
cJSON *discover_models(const cJSON *config, HANDLE cancel, char error[512]);
int inspect_gguf(cJSON *model, HANDLE cancel, char error[512]);
int legacy_template_supported(const char *format);
int legacy_prompt(Buffer *out, const char *format, const cJSON *messages, const char *system, char error[512]);
wchar_t *argv_command_line(const wchar_t *const *argv);
#endif

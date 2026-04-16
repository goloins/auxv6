#ifndef AUXV6_USER_ACCOUNTDB_H
#define AUXV6_USER_ACCOUNTDB_H

#define ADB_FILE_MAX       (256 * 1024)
#define ADB_NAME_MAX       256
#define ADB_PASSWD_MAX     256
#define ADB_GECOS_MAX      1024
#define ADB_PATH_MAX       1024
#define ADB_SHELL_MAX      1024
#define ADB_MEMBERS_MAX    (64 * 1024)

struct adb_passwd_entry {
  char name[ADB_NAME_MAX];
  char passwd[ADB_PASSWD_MAX];
  int uid;
  int gid;
  char gecos[ADB_GECOS_MAX];
  char home[ADB_PATH_MAX];
  char shell[ADB_SHELL_MAX];
};

struct adb_group_entry {
  char name[ADB_NAME_MAX];
  char passwd[ADB_PASSWD_MAX];
  int gid;
  char members[ADB_MEMBERS_MAX];
};

int adb_is_valid_name(const char *s);

int adb_read_file(const char *path, char *buf, int bufsz, int *n_out);
int adb_write_file_atomic(const char *path, const char *tmp_path, const char *buf, int n);

int adb_parse_passwd_line(const char *line, int linelen, struct adb_passwd_entry *out);
int adb_parse_group_line(const char *line, int linelen, struct adb_group_entry *out);

int adb_find_user_by_name(const char *buf, int n, const char *name, struct adb_passwd_entry *out);
int adb_find_user_by_uid(const char *buf, int n, int uid, struct adb_passwd_entry *out);
int adb_find_group_by_name(const char *buf, int n, const char *name, struct adb_group_entry *out);
int adb_find_group_by_gid(const char *buf, int n, int gid, struct adb_group_entry *out);

int adb_next_uid(const char *buf, int n, int min_uid);
int adb_next_gid(const char *buf, int n, int min_gid);

int adb_append_passwd_line(char *dst, int *cur, int max, const struct adb_passwd_entry *ent);
int adb_append_group_line(char *dst, int *cur, int max, const struct adb_group_entry *ent);
int adb_append_raw_line(char *dst, int *cur, int max, const char *line, int linelen);

int adb_group_has_member(const struct adb_group_entry *gr, const char *name);
int adb_group_add_member(struct adb_group_entry *gr, const char *name);
int adb_group_remove_member(struct adb_group_entry *gr, const char *name);

#endif

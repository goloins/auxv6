#ifndef AUXV6_EXT_JOURNAL_H
#define AUXV6_EXT_JOURNAL_H

struct ext2_mount_data;

int ext3_journal_discover(struct ext2_mount_data *data);

#endif
# Filesystem implementation

A naive file-system application allowing simple operations such as making a file system (mkfs), mounting a fs (use), copying a file from current folder to mounted fs or between mounted filesystems (cp), moving a file between mounted filesystems (mv), removing a file from mounted fs (rm) and listing all the files in a mounted fs (ls).

gcc myfs.c -lm
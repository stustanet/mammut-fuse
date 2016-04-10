/*
   Big Brother File System
   Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

   This program can be distributed under the terms of the GNU GPLv3.
   See the file COPYING.

   This code is derived from function prototypes found /usr/include/fuse/fuse.h
   Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
   His code is licensed under the LGPLv2.
   A copy of that code is included in the file fuse.h

   The point of this FUSE filesystem is to provide an introduction to
   FUSE.  It was my first FUSE filesystem as I got to know the
   software; hopefully, the comments in this code will help people who
   follow later to get a gentler introduction.

   This might be called a no-op filesystem:  it doesn't impose
   filesystem semantics on top of any other existing structure.  It
   simply reports the requests that come in, and passes them to an
   underlying filesystem.  The information is saved in a logfile named
   bbfs.log, in the directory from which you run bbfs.

   gcc -Wall `pkg-config fuse --cflags --libs` -o bbfs bbfs.c
   */

#define FUSE_USE_VERSION 26
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <bsd/string.h>
#include <linux/limits.h>
#include <libconfig.h>
#include <sys/types.h> // stat()
#include <sys/stat.h>  // stat()
#include <pwd.h>        // getpwdnam()

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

config_t cfg;

enum mammut_public_type
{
    PUBLIC_NO,
    PUBLIC_NORMAL,
    PUBLIC_ANON
};

struct dirmap_item_st
{
    enum mammut_public_type type;
    char *export;
    char *path;
    char *base;
};

static struct {
    char *userid;
    char **raids;
    size_t raid_count;
    char *public_path;
    char *anon_path;
    char *shared_export_name;
    struct dirmap_item_st *dirmap;
    size_t num_of_dirmaps;
    //char *user_basepath;
} mammut_data;

static struct {
    int anonymous_uid;
    int anonymous_gid;
} global_data;

enum mammut_path_mode {
    MODE_HOMEDIR,
    MODE_LISTDIR_SHARED,
    MODE_PIPETHROUGH_ANON,
    MODE_PIPETHROUGH,
};

enum mammut_path_type
{
    PATH_TYPE_ROOT,
    PATH_TYPE_HOMEDIR,
    PATH_TYPE_PUBLICDIR,
    PATH_TYPE_ANON_DIR,
    PATH_TYPE_PRIVATEDIR
};

// Report errors to logfile and give -errno to caller
static int mammut_error(const char *str)
{
    int ret = -errno;
    printf("Error %s\n", str);
    return ret;
}

static const char *mapping_file_path;

static time_t mapping_last_modification_time = 0;

static struct anon_mapping_st {
    char *mapped;
    char *original;
    char *userid;
} *anon_mappings = 0;

static size_t anon_mappings_count = 0;
static size_t anon_map_buffer_size = 0;

static char *shared_listing = 0;
static size_t shared_listing_buffer_size = 0;

static time_t last_shared_listing_update = 0;
static time_t shared_listing_update_rate = 60;

/**
 * reads the anonymous mapping into the datastructure mapping
 * only read the file if there was a change
 */
static int _mammut_read_anonymous_mapping()
{
    struct stat mapping_stat;
    if(stat(mapping_file_path, &mapping_stat) == 0)
    {
        if(mapping_stat.st_mtime != mapping_last_modification_time) // we have to reread
        {
            // free anon_mappings before reallocating...
            for(int i = 0; i < anon_mappings_count; i++)
            {
                free(anon_mappings[i].mapped);
                free(anon_mappings[i].original);
                free(anon_mappings[i].userid);
            }

            FILE *mapping = fopen(mapping_file_path, "r");

            if(!mapping)
            {
                printf("Anon mapping file not found");
                return -ENOENT;
            }

            anon_mappings_count = 0;

            char *mapped = 0, *orig = 0, *user = 0;

            // whitespace at end eats newline and spaces
            while(fscanf(mapping, "%m[^/]/%m[^/]/%m[^\n] ", &mapped, &user, &orig) == 3)
            {
                if(anon_mappings_count >= anon_map_buffer_size)
                {
                    size_t newsize = anon_map_buffer_size * 2;
                    if(newsize < 32)
                    {
                        newsize = 32;
                    }

                    struct anon_mapping_st *tmp = (struct anon_mapping_st*) realloc(
                            anon_mappings,
                            sizeof(struct anon_mapping_st) * newsize);

                    if(tmp == 0)
                    {
                        printf("Failed to allocate %i anon map entries.", (int)newsize);
                        free(mapped);
                        free(orig);
                        free(user);
                        return -ENOMEM;
                    }
                    else
                    {
                        anon_map_buffer_size = newsize;
                        anon_mappings = tmp;
                    }
                }

                anon_mappings[anon_mappings_count].mapped = mapped;
                anon_mappings[anon_mappings_count].original = orig;
                anon_mappings[anon_mappings_count].userid = user;

                anon_mappings_count++;

                printf("map: %s → %s(%s)\n", mapped, orig, user);

                mapped = orig = user = 0;
            }

            if(mapped) free(mapped);
            if(orig)   free(orig);
            if(user)   free(user);

            fclose(mapping);

            last_shared_listing_update = 0;
            mapping_last_modification_time = mapping_stat.st_mtime;
        }
        else
        {
            printf("Anon map is up to date\n");
        }

    }
    else
    {
        printf("Anon mapping file not found\n");
        return -ENOENT;
    }
    return 0;
}

/**
 * Search in raids for a RAID/SUBDIR/USERID and write RAID into fpath.
 */
static int _mammut_locate_userdir(char fpath[PATH_MAX], const char *userid, const char *subdir)
{

    size_t i;

    for (i = 0; i < mammut_data.raid_count; i++)
    {
        if (PATH_MAX > snprintf(fpath, PATH_MAX, "%s/%s/%s", mammut_data.raids[i], subdir, userid)
            && access(fpath, F_OK) == 0)
        {
            fprintf(stderr, "userid: %s, subdir %s fpath: %s\n", userid, subdir, fpath);
            return 0;
        }
    }
///Locate xfs user filesystem

    fprintf(stderr, "FAIIIIIILL userid: %s, subdir %s last check: %s\n", userid, subdir, fpath);
    return -ENOENT;
}

/**
 * Resolves SUBDIR as export path and calls _mammut_locate_userdir
 * type returns, if this path is defined as public path
 */
static int _mammut_locate_mapped_userdir(char fpath[PATH_MAX], const char *userid, const char *subdir, enum mammut_public_type *type)
{

    size_t dm;
    for(dm = 0; dm < mammut_data.num_of_dirmaps; dm++)
    {
        if(strcmp(subdir, mammut_data.dirmap[dm].export) == 0)
        {
            (*type) = mammut_data.dirmap[dm].type;
            if(mammut_data.dirmap[dm].base != 0)
            {
                strlcpy(fpath, mammut_data.dirmap[dm].base, PATH_MAX);
                strlcat(fpath, "/", PATH_MAX);
                strlcat(fpath, userid, PATH_MAX);
                if(mammut_data.dirmap[dm].path != 0)
                {
                    strlcat(fpath, "/", PATH_MAX);
                    strlcat(fpath, mammut_data.dirmap[dm].path, PATH_MAX);
                }
                fprintf(stderr, "basemapping: %s\n", fpath);
                return 0;
            }
            else
            {
                return _mammut_locate_userdir(fpath, userid, mammut_data.dirmap[dm].path);
            }
        }
    }

    fprintf(stderr, "Dir not mapped: %s\n", subdir);
    return -ENOENT;
}

/**
 * Returns the original anonymous directory in fpath. anon_dir contains the mapped directory name
 */
static int _mammut_locate_anondir(char fpath[PATH_MAX], const char *anon_dir)
{
    // lookup mapping from anon_dir to the original directory
    for(size_t i = 0; i < anon_mappings_count; i++)
    {
        if(strcmp(anon_mappings[i].mapped, anon_dir) == 0)
        {
            for (int j = 0; j < mammut_data.raid_count; j++)
            {
                if (PATH_MAX > snprintf(fpath,
                            PATH_MAX,
                            "%s/%s/%s/%s",
                            mammut_data.raids[j],
                            mammut_data.anon_path,
                            anon_mappings[i].userid,
                            anon_mappings[i].original)
                    && access(fpath, F_OK) != -1)
                {
                    return 0;
                }
            }
            break;
        }
    }
    return -ENOENT;
}

/**
 * Recursive chmod, path may be file or directory
 */
static void _chmod_recursive(const char *path, int mode, int mask)
{
    struct stat st;
    int s = stat(path, &st);
    if(s == 0)
    {
        chmod(path, (st.st_mode & ~mask) | (mode & mask));

        DIR *dp = opendir(path);
        if(dp != 0)
        {
            struct dirent *dirent;
            for (dirent = readdir(dp); dirent ; dirent = readdir(dp))
            {
                char sn_path[PATH_MAX];
                strlcpy(sn_path, path, sizeof(sn_path));
                strlcat(sn_path, "/", sizeof(sn_path));
                strlcat(sn_path, dirent->d_name, sizeof(sn_path));
                _chmod_recursive(sn_path, mode, mask);
            }
            closedir(dp);
        }
    }
}


/**
 * Translate the Filesystem-Address (/public/XX, /private, ...) to one of the following:
 *  * Home-Directory: /
 *  * First order subdirectories: public, private, anonymous, list-anonymous, list-public, backup
 *  * Second order subdirectories: user defined.
 *  * the mode-parameter indicates what kind of directory is found at path (RW / RO / Lists )
 */
static int mammut_fullpath(char fpath[PATH_MAX],
        const char *path,
        enum mammut_path_mode* mode,
        enum mammut_path_type *type)
{
    //strukutur pfad public/private.../: ./public → /"raid"/public/USERID/
    char *my_path = strdup(path);
    if(my_path == NULL) return -ENOMEM;

    char *token;
    char *saveptr;

    *mode = MODE_HOMEDIR;
    //strcpy(fpath, mammut_data.user_basepath);
    fpath[0] = 0;

    enum mammut_public_type pt = PUBLIC_NO;

    // Get first path element
    if (!(token = strtok_r(my_path, "/", &saveptr)))
    {
        if(type) *type = PATH_TYPE_ROOT;

        free(my_path);
        return 0;
    }
    else if (!strcmp(token, mammut_data.shared_export_name))
    {
        pt = PUBLIC_NORMAL;
        *mode = MODE_LISTDIR_SHARED;
        if(type) *type = PATH_TYPE_HOMEDIR;
    }
    else
    {
        if(mammut_data.userid == 0)
        {
            free(my_path);
            return -ENOENT;
        }

        int retstat = _mammut_locate_mapped_userdir(fpath, mammut_data.userid, token, &pt);
        if(retstat != 0)
        {
            //Non-existent entry
            free(my_path);
            return retstat;
        }

        *mode = MODE_PIPETHROUGH;
        if(type) *type = PATH_TYPE_HOMEDIR;

    }

    // Check second path element
    if(!(token = strtok_r(NULL, "/", &saveptr)))
    {
        free(my_path);
        return 0;
    }
    else if (*mode == MODE_LISTDIR_SHARED)
    {

        _mammut_read_anonymous_mapping(); // this is a caching function

        int retstat = _mammut_locate_anondir(fpath, token);
        if(retstat != 0)
        {
            // NOT anon
            *mode = MODE_PIPETHROUGH;
            if(type) *type = PATH_TYPE_PUBLICDIR;

            retstat = _mammut_locate_userdir(fpath, token, mammut_data.public_path);
            if(retstat != 0)
            {
                free(my_path);
                return retstat;
            }
        }
        else
        {
            *mode = MODE_PIPETHROUGH_ANON;
            if(type) *type = PATH_TYPE_PUBLICDIR;
        }

        /* strcat(fpath, "/anonymous"); */
    }
    else
    {
        if(type)
        {
            switch(pt)
            {
                case PUBLIC_NORMAL:
                    *type = PATH_TYPE_PUBLICDIR;
                    break;
                case PUBLIC_ANON:
                    *type = PATH_TYPE_ANON_DIR;
                    break;
                default:
                    *type = PATH_TYPE_PRIVATEDIR;
            }
        }
        strlcat(fpath, "/", PATH_MAX);
        strlcat(fpath, token, PATH_MAX);
    }

    while((token = strtok_r(NULL, "/", &saveptr))){
        if (*mode == MODE_PIPETHROUGH
         || *mode == MODE_PIPETHROUGH_ANON)
        {
            if(type) *type = (pt != PUBLIC_NO) ? PATH_TYPE_PUBLICDIR : PATH_TYPE_PRIVATEDIR;
            strlcat(fpath, "/", PATH_MAX);
            strlcat(fpath, token, PATH_MAX);
        }
    };

    printf("mammut_fullpath: fPath: %s last token: %s Mode: %i\n", fpath, token, *mode);
    free(my_path);
    return 0;
}

/**
 * Updates the shared listing, if required
 */
static int _load_shared_listing()
{
    _mammut_read_anonymous_mapping(); // this method is efficient and only reads on modification

    struct timeval tv;
    gettimeofday(&tv, 0);

    if(last_shared_listing_update + shared_listing_update_rate < tv.tv_sec)
    {
        printf("Update shared listingi\n");

        if(shared_listing_buffer_size == 0)
        {
            shared_listing_buffer_size = 1024;
            shared_listing = (char *)malloc(shared_listing_buffer_size);
        }

        size_t shared_used = 1;

        for (size_t i = 0; i < mammut_data.raid_count; ++i)
        {
            char publicPath[PATH_MAX];
            DIR *cur_raid;
            printf("Running raid %s\n", mammut_data.raids[i]);

            strlcpy(publicPath, mammut_data.raids[i], sizeof(publicPath));
            strlcat(publicPath, "/", sizeof(publicPath));
            strlcat(publicPath, mammut_data.public_path, sizeof(publicPath));
            cur_raid = opendir(publicPath);
            if(cur_raid != 0)
            {
                struct dirent *dirent;

                for (dirent = readdir(cur_raid); dirent ; dirent = readdir(cur_raid))
                {
                    // eat the . and .. of the raid-dirs
                    if(strncmp(dirent->d_name, ".", 1) == 0 || strncmp(dirent->d_name, "..", 2) == 0)
                        continue;

                    char tmppath[PATH_MAX];
                    strlcpy(tmppath, publicPath, sizeof(tmppath));
                    strlcat(tmppath, "/", sizeof(tmppath));
                    strlcat(tmppath, dirent->d_name, sizeof(tmppath));

                    DIR *test_empty = opendir(tmppath);
                    if(test_empty == 0) continue;

                    int count;
                    for(count = 0; count < 3 && readdir(test_empty); count++);

                    closedir(test_empty);

                    if(count < 3) continue;

                    printf("Adding public entry %s\n", dirent->d_name);

                    size_t reqsize = shared_used + strlen(dirent->d_name) + 1;
                    if(reqsize > shared_listing_buffer_size)
                    {
                        size_t newsize = shared_listing_buffer_size * 2;
                        char *tmp = (char *)realloc(shared_listing, newsize);
                        if(tmp == 0)
                        {
                            shared_listing[shared_used - 1] = 0;
                            return -ENOMEM;
                        }
                        shared_listing_buffer_size = newsize;
                        shared_listing = tmp;
                    }

                    strcpy(shared_listing + (shared_used - 1), dirent->d_name);

                    shared_used = reqsize;

                }

                closedir(cur_raid);
            }
        }

        for (size_t i = 0; i < anon_mappings_count; i++)
        {
            printf("Adding anon entry %s\n", anon_mappings[i].mapped);

            size_t reqsize = shared_used + strlen(anon_mappings[i].mapped) + 1;
            if(reqsize > shared_listing_buffer_size)
            {
                size_t newsize = shared_listing_buffer_size * 2;
                char *tmp = (char *)realloc(shared_listing, newsize);
                if(tmp == 0)
                {
                    shared_listing[shared_used - 1] = 0;
                    return -ENOMEM;
                }
                shared_listing_buffer_size = newsize;
                shared_listing = tmp;
            }

            strcpy(shared_listing + (shared_used - 1), anon_mappings[i].mapped);

            shared_used = reqsize;
        }

        shared_listing[shared_used - 1] = 0;

        last_shared_listing_update = tv.tv_sec;
    }

    return 0;
}

static int _check_name_sanity(const char *name)
{
    if(strlen(name) >= 64) return 0;
    wchar_t uc[64];

    int len = mbstowcs(uc, name, 64);
    for(int i = 0; i < len; i++)
    {
        if(!isprint(uc[i])) return 0;
    }

    return 1;
}

static int _is_dir(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    
}
///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
static int mammut_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    enum mammut_path_mode mode;
    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    switch(mode)
    {
        case MODE_HOMEDIR:
        case MODE_LISTDIR_SHARED:
            printf("Getattr of homedir\n");
            statbuf->st_dev = 0;               // IGNORED Device
            statbuf->st_ino = 999;             // IGNORED inode number
            statbuf->st_mode = S_IFDIR | 0755; // Protection
            statbuf->st_nlink = 0;             // Number of Hard links
            statbuf->st_uid = geteuid();       // Group ID of owner
            statbuf->st_gid = getegid();       // User ID of owner
            statbuf->st_rdev = 1;
            statbuf->st_size = 1;
            statbuf->st_blksize = 1;           // IGNORED
            statbuf->st_blocks = 1;
            statbuf->st_atim.tv_sec = 1;       // Last Access
            statbuf->st_mtim.tv_sec = 1;       // Last Modification
            statbuf->st_ctim.tv_sec = 1;       // Last Status change
            return 0;
        case MODE_PIPETHROUGH_ANON:
        case MODE_PIPETHROUGH:
            if ((retstat = lstat(fpath, statbuf)))
            {
                retstat = mammut_error("mammut_getattr lstat");
            }
            break;
    }

    if (mode == MODE_PIPETHROUGH_ANON)
    {
        // Eliminate all User-IDs from the file
        statbuf->st_uid = global_data.anonymous_uid;
        statbuf->st_gid = global_data.anonymous_gid;

    }

    return retstat;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// Note the system readlink() will truncate and lose the terminating
// null.  So, the size passed to to the system readlink() must be one
// less than the size passed to mammut_readlink()
// mammut_readlink() code by Bernardo F Costa (thanks!)
static int mammut_readlink(const char *path, char *link, size_t size)
{
    (void)path;
    (void)link;
    (void)size;

    return -ENOTSUP;
}

/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?
static int mammut_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void)path;
    (void)mode;
    (void)dev;
    return -ENOTSUP;
}

/** Create a directory */
static int mammut_mkdir(const char *path, mode_t mode)
{
    const char *dn = strrchr(path, '/');
    if(!_check_name_sanity(dn)) return -EINVAL;

    int retstat = 0;
    char fpath[PATH_MAX];
    enum mammut_path_mode mammut_mode;

    enum mammut_path_type type;
    retstat = mammut_fullpath(fpath, path, &mammut_mode, &type);
    if(retstat != 0) return retstat;

    // prevent inaccessable dirs in public dirs
    if(type == PATH_TYPE_PUBLICDIR || type == PATH_TYPE_ANON_DIR) mode = ((mode & 0770) | 0005);

    retstat = mkdir(fpath, mode);
    if (retstat < 0)
        retstat = mammut_error("mammut_mkdir mkdir");

    return retstat;
}

/** Remove a file */
static int mammut_unlink(const char *path)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    enum mammut_path_mode mode;

    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    retstat = unlink(fpath);
    if (retstat < 0)
        retstat = mammut_error("mammut_unlink unlink");

    return retstat;
}

/** Remove a directory */
static int mammut_rmdir(const char *path)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    enum mammut_path_mode mode;

    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    retstat = rmdir(fpath);
    if (retstat < 0)
        retstat = mammut_error("mammut_rmdir rmdir");

    return retstat;
}

/** Create a symbolic link */
// The parameters here are a little bit confusing, but do correspond
// to the symlink() system call.  The 'path' is where the link points,
// while the 'link' is the link itself.  So we need to leave the path
// unaltered, but insert the link into the mounted directory.
static int mammut_symlink(const char *path, const char *link)
{
    (void)path;
    (void)link;

    return -ENOTSUP;
}

/** Rename a file */
// both path and newpath are fs-relative
static int mammut_rename(const char *path, const char *newpath)
{
    const char *dn = strrchr(newpath, '/');
    if(!_check_name_sanity(dn)) return -EINVAL;
    
    int retstat = 0;
    char fpath[PATH_MAX];
    char fnewpath[PATH_MAX];

    enum mammut_path_mode mode;

    enum mammut_path_type type_orig, type_new;
    retstat = mammut_fullpath(fpath, path, &mode, &type_orig);
    if(retstat != 0) return retstat;

    retstat = mammut_fullpath(fnewpath, newpath, &mode, &type_new);
    if(retstat != 0) return retstat;

    if(!_is_dir(fpath) && type_new == PATH_TYPE_ANON_DIR) return -EPERM;

    retstat = rename(fpath, fnewpath);
    if (retstat < 0)
    {
        retstat = mammut_error("mammut_rename rename");
    }
    else if(type_orig == PATH_TYPE_PRIVATEDIR && (type_new == PATH_TYPE_PUBLICDIR || type_new == PATH_TYPE_ANON_DIR))
    {
        //move private -> public => chmod 755
        _chmod_recursive(newpath, 0005, 0007);
    }

    return retstat;
}

/** Create a hard link to a file */
static int mammut_link(const char *path, const char *newpath)
{
    (void)path;
    (void)newpath;

    return -ENOTSUP;
    /*
    int retstat = 0;
    char fpath[PATH_MAX], fnewpath[PATH_MAX];

    meammut_fullpath(fpath, path, &mode);
    mammut_fullpath(fnewpath, newpath);

    retstat = link(fpath, fnewpath);
    if (retstat < 0)
        retstat = mammut_error("mammut_link link");

    return retstat;
    */
}

/** Change the permission bits of a file */
static int mammut_chmod(const char *path, mode_t mode)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    enum mammut_path_mode mammut_mode;

    enum mammut_path_type type;
    retstat = mammut_fullpath(fpath, path, &mammut_mode, &type);
    if(retstat != 0) return retstat;

    if(type != PATH_TYPE_PRIVATEDIR && type != PATH_TYPE_PUBLICDIR && type != PATH_TYPE_ANON_DIR)
       return -EPERM;

    // prevent inaccessable items in public and write access on foreign items
    if(type == PATH_TYPE_PUBLICDIR || type == PATH_TYPE_ANON_DIR) mode = ((mode & 0770) | 0005);

    retstat = chmod(fpath, mode);
    if (retstat < 0)
        retstat = mammut_error("mammut_chmod chmod");

    return retstat;
}

/** Change the owner and group of a file */
static int mammut_chown(const char *path, uid_t uid, gid_t gid)
{
    (void)path;
    (void)uid;
    (void)gid;
    return -EPERM;
}

/** Change the size of a file */
static int mammut_truncate(const char *path, off_t newsize)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    enum mammut_path_mode mode;
    printf("truncate src path : %s\n", path);

    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;
    
    if(newsize > (1ULL << 31))
    {
       struct stat st;
       if(stat(fpath, &st) != 0)
       {
           return -errno;
       }

       if(st.st_size < newsize)
       {
           return -EPERM;
       }
    }

    retstat = truncate(fpath, newsize);
    if (retstat < 0)
        mammut_error("mammut_truncate truncate");

    return retstat;
}

/** Change the access and/or modification times of a file */
/* note -- I'll want to change this as soon as 2.6 is in debian testing */
static int mammut_utime(const char *path, struct utimbuf *ubuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    enum mammut_path_mode mode;
    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    retstat = utime(fpath, ubuf);
    if (retstat < 0)
        retstat = mammut_error("mammut_utime utime");

    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
static int mammut_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    int fd;
    char fpath[PATH_MAX];
    enum mammut_path_mode mode;

    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    fd = open(fpath, fi->flags);
    if (fd < 0)
        retstat = mammut_error("mammut_open open");
    fi->fh = fd;

    printf("Opening file %s ret: %i fd %i\n", fpath, retstat, fd);

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
static int mammut_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void)path;

    int retstat = pread(fi->fh, buf, size, offset);
    if (retstat < 0)
        retstat = mammut_error("mammut_read read");

    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.
static int mammut_write(const char *path, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    (void) path;
    int retstat = 0;

    retstat = pwrite(fi->fh, buf, size, offset);
    if (retstat < 0)
        retstat = mammut_error("mammut_write pwrite");

    return retstat;
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
static int mammut_statfs(const char *path, struct statvfs *statv)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    enum mammut_path_mode mode;

    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    // get stats for underlying filesystem
    retstat = statvfs(fpath, statv);
    if (retstat < 0)
        retstat = mammut_error("mammut_statfs statvfs");

    return retstat;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
static int mammut_flush(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    (void)fi;

    int retstat = 0;

    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
static int mammut_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    int retstat = 0;

    // We need to close the file.  Had we allocated any resources
    // (buffers etc) we'd need to free them here as well.
    retstat = close(fi->fh);

    return retstat;
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
static int mammut_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    (void)path;
    (void)datasync;

    int retstat = 0;

    // some unix-like systems (notably freebsd) don't have a datasync call
#ifdef HAVE_FDATASYNC
    if (datasync)
        retstat = fdatasync(fi->fh);
    else
#endif
        retstat = fsync(fi->fh);

    if (retstat < 0)
        mammut_error("mammut_fsync fsync");

    return retstat;
}

#ifdef HAVE_SYS_XATTR_H
/** Set extended attributes */
static int mammut_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;
    return ENOTSUP;
}

/** Get extended attributes */
static int mammut_getxattr(const char *path, const char *name, char *value, size_t size)
{
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    return ENOTSUP;
}

/** List extended attributes */
static int mammut_listxattr(const char *path, char *list, size_t size)
{
    (void)path;
    (void)list;
    (void)size;
    return ENOTSUP;
}

/** Remove extended attributes */
static int mammut_removexattr(const char *path, const char *name)
{
    (void)path;
    (void)list;
    return ENOTSUP;
}
#endif

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
static int mammut_opendir(const char *path, struct fuse_file_info *fi)
{
    printf("\n\nOpendir %s\n\n", path);
    DIR *dp;
    int retstat = 0;
    char fpath[PATH_MAX];

    enum mammut_path_mode mode;
    retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    switch (mode)
    {
        case MODE_LISTDIR_SHARED:
            break;

        case MODE_PIPETHROUGH_ANON:
        case MODE_PIPETHROUGH:
            dp = opendir(fpath);
            if (dp == NULL)
            {
                retstat = mammut_error("mammut_opendir opendir");
                return -errno;
            }

            fi->fh = (intptr_t) dp;
            break;
        case MODE_HOMEDIR:

            break;
    }
    return 0;
}


/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
static int mammut_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
        struct fuse_file_info *fi)
{
    (void)offset;
    (void)fi;

    int retstat = 0;
    char fPath [PATH_MAX];

    // once again, no need for fullpath -- but note that I need to cast fi->fh
    //dp = (DIR *) (uintptr_t) fi->fh;

    enum mammut_path_mode mode;
    retstat = mammut_fullpath(fPath, path, &mode, 0);
    if(retstat != 0) return retstat;

    // Every directory contains at least two entries: . and ..  If my
    // first call to the system readdir() returns NULL I've got an
    // error; near as I can tell, that's the only condition under
    // which I can get an error from readdir()
    //if (de == 0) {
    //    retstat = mammut_error("mammut_readdir readdir");
    //    return retstat;
    //}

    // This will copy the entire directory into the buffer.  The loop exits
    // when either the system readdir() returns NULL, or filler()
    // returns something non-zero.  The first case just means I've
    // read the whole directory; the second means the buffer is full.
    switch (mode)
    {
        case MODE_PIPETHROUGH:
        case MODE_PIPETHROUGH_ANON:
            printf("RO/ROW/ANON Iterating through path %s\n", fPath);
            {
                DIR *dp = (DIR *) (uintptr_t) (fi->fh);

                if(dp == 0) return EINVAL;

                rewinddir(dp);

                struct dirent *de = readdir(dp);
                do {
                    if (filler(buf, de->d_name, NULL, 0) != 0)
                        return -ENOMEM;
                } while ((de = readdir(dp)) != NULL);
            }
            break;
        case MODE_HOMEDIR:
            filler(buf, ".", NULL, 0);
            filler(buf, "..", NULL, 0);
            if(mammut_data.userid != 0)
            {
                for(size_t i = 0; i < mammut_data.num_of_dirmaps; i++)
                {
                    filler(buf, mammut_data.dirmap[i].export, NULL, 0);
                }
            }

            filler(buf, mammut_data.shared_export_name, NULL, 0);
            break;
        case MODE_LISTDIR_SHARED:
            printf("\n\nLIST PUBLIC %s\n\n", fPath);
            retstat = _load_shared_listing();
            if(retstat != 0) return retstat;

            filler(buf, ".", NULL, 0);
            filler(buf, "..", NULL, 0);

            const char *cur;
            for(cur = shared_listing; cur[0] != 0; cur += strlen(cur) + 1)
            {
                if(cur - shared_listing >= shared_listing_buffer_size) break;

                if (filler(buf, cur, NULL, 0) != 0)
                    return -ENOMEM;

            }

            break;
    }

    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
static int mammut_releasedir(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    int retstat = 0;

    closedir((DIR *) (uintptr_t) fi->fh);

    return retstat;
}

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
// when exactly is this called?  when a user calls fsync and it
// happens to be a directory? ???
static int mammut_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
    (void)path;
    (void)datasync;
    (void)fi;
    int retstat = 0;

    return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *mammut_init(struct fuse_conn_info *conn)
{
    (void) conn;

    return &mammut_data;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void mammut_destroy(void *userdata)
{
    (void)userdata;
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
static int mammut_access(const char *path, int mask)
{
    char fpath[PATH_MAX];

    enum mammut_path_mode mode;

    int retstat = mammut_fullpath(fpath, path, &mode, 0);
    if(retstat != 0) return retstat;

    switch (mode) {
        case MODE_HOMEDIR:
        case MODE_LISTDIR_SHARED:
            if ((mask & W_OK) == W_OK ) return -1;
            else return 0;
            break;

        case MODE_PIPETHROUGH_ANON:
        case MODE_PIPETHROUGH:
            return access(fpath, mask);
            break;
        default:
            return -1;
    }
    return -1;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
static int mammut_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    const char *dn = strrchr(path, '/');
    if(!_check_name_sanity(dn)) return -EINVAL;
    
    int retstat = 0;
    char fpath[PATH_MAX];
    int fd;

    enum mammut_path_mode mammut_mode;
    enum mammut_path_type type;

    retstat = mammut_fullpath(fpath, path, &mammut_mode, &type);
    if(retstat != 0) return retstat;

    //prevent files in anon
    if(type == PATH_TYPE_ANON_DIR) return -EPERM;

    // prevent inaccessable files in public dirs
    if(type == PATH_TYPE_PUBLICDIR) mode = ((mode & 0770) | 0005);

    fd = creat(fpath, mode);
    if (fd < 0)
        retstat = mammut_error("mammut_create creat");

    fi->fh = fd;

    return retstat;
}

struct fuse_operations mammut_oper = {
    .getattr = mammut_getattr,
    .readlink = mammut_readlink,
    // no .getdir -- that's deprecated
    .getdir = NULL,
    .mknod = mammut_mknod,
    .mkdir = mammut_mkdir,
    .unlink = mammut_unlink,
    .rmdir = mammut_rmdir,
    .symlink = mammut_symlink,
    .rename = mammut_rename,
    .link = mammut_link,
    .chmod = mammut_chmod,
    .chown = mammut_chown,
    .truncate = mammut_truncate,
    .utime = mammut_utime,
    .open = mammut_open,
    .read = mammut_read,
    .write = mammut_write,
    /** Just a placeholder, don't set */ // huh???
    .statfs = mammut_statfs,
    .flush = mammut_flush,
    .release = mammut_release,
    .fsync = mammut_fsync,

#ifdef HAVE_SYS_XATTR_H
    .setxattr = mammut_setxattr,
    .getxattr = mammut_getxattr,
    .listxattr = mammut_listxattr,
    .removexattr = mammut_removexattr,
#endif

    .opendir = mammut_opendir,
    .readdir = mammut_readdir,
    .releasedir = mammut_releasedir,
    .fsyncdir = mammut_fsyncdir,
    .init = mammut_init,
    .destroy = mammut_destroy,
    .access = mammut_access,
    .create = mammut_create,
    //.ftruncate = mammut_ftruncate,
    //.fgetattr = mammut_fgetattr
};

void mammut_usage()
{
    fprintf(stderr, "usage:  mammutfs [fuseopts] mountpoint configfile userid\n");
    exit(EXIT_FAILURE);
}



//mammutfs [fuseopts] mountpoint onfigfile userid
// should be ./mammutfs userid /mnt/dir (userid could be read)
int main(int argc, char *argv[])
{
    int fuse_stat;

    // bbfs doesn't do any access checking on its own (the comment
    // blocks in fuse.h mention some of the functions that need
    // accesses checked -- but note there are other functions, like
    // chown(), that also need checking!).  Since running bbfs as root
    // will therefore open Metrodome-sized holes in the system
    // security, we'll check if root is trying to mount the filesystem
    // and refuse if it is.  The somewhat smaller hole of an ordinary
    // user doing it with the allow_other flag is still there because
    // I don't want to parse the options string.
    if ((getuid() == 0) || (geteuid() == 0)) {
        fprintf(stderr, "Running MAMMUTFS as root opens unnacceptable security holes\n");
        return 1;
    }
    //
    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments
    if (argc < 4)
        mammut_usage();

    const char *mammutfs_config_file = argv[argc-2];

    // read config file
    config_init(&cfg);
    if (!config_read_file(&cfg, mammutfs_config_file)) {
        printf("%s:%d - %s\n",
                config_error_file(&cfg),
                config_error_line(&cfg),
                config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }

    const char *_CONFIG_ANON_MAPPING;
    if(config_lookup_string(&cfg, "anon_mapping", &_CONFIG_ANON_MAPPING) != CONFIG_TRUE)
    {
        printf("Invalid config: \"anon_mapping\" not found");
        return EXIT_FAILURE;
    }

    mapping_file_path = strdup(_CONFIG_ANON_MAPPING);
    printf("Anonymous directory mapping file: %s\n", mapping_file_path);

    const char *configAnonUser = "nobody";
    config_lookup_string(&cfg, "anon_user", &configAnonUser);

    configAnonUser = strdup(configAnonUser);

    const config_setting_t *raids;
    raids = config_lookup(&cfg, "raids");
    mammut_data.raid_count = config_setting_length(raids);
    mammut_data.raids = (char **)malloc(mammut_data.raid_count * sizeof(char *));

    printf("used raids:\n");
    for(unsigned int i = 0; i < mammut_data.raid_count; i++)
    {

        // this string is freed when the config is destroyed
        const char *raid = config_setting_get_string_elem(raids, i);
        mammut_data.raids[i] = strdup(raid);
        printf("\t%s\n",mammut_data.raids[i]);
    }
    
    const char *shared_export_name;
    if(config_lookup_string(&cfg, "shared_export", &shared_export_name) != CONFIG_TRUE)
    {
        printf("Invalid config: \"shared_export\" not found\n");
        return EXIT_FAILURE;
    }
    mammut_data.shared_export_name = strdup(shared_export_name);

    const char *public_path;
    if(config_lookup_string(&cfg, "public_path", &public_path) != CONFIG_TRUE)
    {
        printf("Invalid config: \"public_path\" not found\n");
        return EXIT_FAILURE;
    }
    mammut_data.public_path = strdup(public_path);
    
    const char *anon_path;
    if(config_lookup_string(&cfg, "anon_path", &anon_path) != CONFIG_TRUE)
    {
        printf("Invalid config: \"anon_path\" not found\n");
        return EXIT_FAILURE;
    }
    mammut_data.anon_path = strdup(anon_path);
    
    
    const config_setting_t *dirmap;
    dirmap = config_lookup(&cfg, "dirmap");
    mammut_data.num_of_dirmaps = config_setting_length(dirmap);
    mammut_data.dirmap = (struct dirmap_item_st *)malloc(mammut_data.num_of_dirmaps * sizeof(struct dirmap_item_st));

    for(unsigned int i = 0; i < mammut_data.num_of_dirmaps; i++)
    {
        const config_setting_t *ent = config_setting_get_elem(dirmap, i);
        const char *path = 0, *export = 0, *base = 0;
        if(config_setting_lookup_string(ent, "export", &export) != CONFIG_TRUE ||
           (config_setting_lookup_string(ent, "path", &path) != CONFIG_TRUE &&
           config_setting_lookup_string(ent, "base", &base) != CONFIG_TRUE))
        {
            printf("Invalid config: Invalid dirmap entry!\n");
            printf("Entry must have \"export\" and (\"path\" or \"base\") defined\n");
            return EXIT_FAILURE;
        }

        int is_public;
        if(config_setting_lookup_int(ent, "ispublic", &is_public) != CONFIG_TRUE)
        {
            is_public = 0;
        }
        else
        {
            is_public = (is_public != 0) ? PUBLIC_NORMAL : PUBLIC_NO;
        }

        if(path != 0 && !strcmp(path, mammut_data.anon_path))
        {
            is_public = PUBLIC_ANON;
        }

        printf("Export %s as %s (public: %i)\n", path ? path : base, export, is_public);

        mammut_data.dirmap[i].export = strdup(export);
        
        if(path)
        {
            mammut_data.dirmap[i].path = strdup(path);
        }
        else
        {
            mammut_data.dirmap[i].path = 0;
        }
        
        if(base)
        {
            mammut_data.dirmap[i].base = strdup(base);
        }
        else
        {
            mammut_data.dirmap[i].base = 0;
        }
        
        mammut_data.dirmap[i].type = is_public;
    }

    config_destroy(&cfg);


    if(strcmp(argv[argc - 1], "0") == 0)
    {
        mammut_data.userid = 0;
        printf("Using anonymous user\n");
    }
    else
    {
        mammut_data.userid = argv[argc-1];
        printf("userid: %s\n", mammut_data.userid);
    }

    /*
    char fPath[PATH_MAX];
    if(mammut_data.userid)
    {
        if(_mammut_locate_userdir(fPath, mammut_data.userid, "public") != 0)
        {
            printf("Failed to loacte user dir!");
            return EXIT_FAILURE;
        }
        mammut_data.user_basepath = strdup(fPath);
    }
    else
    {
        mammut_data.user_basepath = "";
    }
    */

    struct passwd *passwd_info = getpwnam(configAnonUser);

    printf("Anon user: %s\n", configAnonUser);

    if(passwd_info == NULL)
    {
        printf("the anonymous user \"%s\" could not be found...\n", configAnonUser);
        return -1;
    }

    global_data.anonymous_uid = passwd_info->pw_uid;
    global_data.anonymous_gid = passwd_info->pw_gid;

    printf("anonymous user id: %i\n", global_data.anonymous_uid);
    printf("anonymous group id: %i\n", global_data.anonymous_gid);


    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc-2, argv, &mammut_oper, &mammut_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}

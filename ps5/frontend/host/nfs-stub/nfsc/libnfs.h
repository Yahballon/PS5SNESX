// (AI-assisted) Minimal libnfs 5 API double for backend failure tests.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

struct nfs_context
{
};
struct nfsfh
{
};
struct nfsdir
{
};
struct nfs_stat_64
{
	uint64_t nfs_size;
	uint64_t nfs_mode;
};
struct nfsdirent
{
	char* name;
	uint32_t type;
	uint64_t size;
};

nfs_context* nfs_init_context();
void nfs_destroy_context(nfs_context*);
void nfs_set_timeout(nfs_context*, int);
void nfs_set_autoreconnect(nfs_context*, int);
int nfs_set_version(nfs_context*, int);
int nfs_mount(nfs_context*, const char*, const char*);
const char* nfs_get_error(nfs_context*);
int nfs_opendir(nfs_context*, const char*, nfsdir**);
nfsdirent* nfs_readdir(nfs_context*, nfsdir*);
void nfs_closedir(nfs_context*, nfsdir*);
int nfs_stat64(nfs_context*, const char*, nfs_stat_64*);
int nfs_lstat64(nfs_context*, const char*, nfs_stat_64*);
int nfs_open(nfs_context*, const char*, int, nfsfh**);
int nfs_close(nfs_context*, nfsfh*);
int nfs_fstat64(nfs_context*, nfsfh*, nfs_stat_64*);
int nfs_pread(nfs_context*, nfsfh*, uint64_t, uint64_t, void*);

void nfs_set_readdir_max_buffer_size(nfs_context*, uint32_t, uint32_t);
int nfs_get_fd(nfs_context*);

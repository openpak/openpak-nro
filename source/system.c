// SPDX-License-Identifier: AGPL-3.0-only
#include "system.h"
#include "hosts.h"
#include "ksp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#ifdef OPENPAK_HOST_TEST
#include <openssl/sha.h>
#define ASSET "romfs/"
static void hash(uint8_t out[32], const void *data, size_t n) { SHA256(data, n, out); }
#else
#include <switch.h>
#include "installtrust.h"
#define ASSET "romfs:/"
static void hash(uint8_t out[32], const void *data, size_t n) { sha256CalculateHash(out, data, n); }
#endif

#define STATE "/switch/openpak/system"
#define PACKAGE "/atmosphere/package3"
#define STORE "/atmosphere/contents/0100000000000800/romfs/ssl_TrustedCerts.bdf"
#define CACHE "/atmosphere/contents/0100000000000800/romfs_metadata.bin"
#define PACKAGE_SIZE 0x800000
#define BOOT "/bootloader/hekate_ipl.ini"
#define OPENPAK_PACKAGE "/atmosphere/package3-openpak"
#define EXOSPHERE "/exosphere.ini"
#define PATCHES "/bootloader/patches.ini"
#define EMUMMC_INI "/emuMMC/emummc.ini"
#define KSP_LOG STATE "/save-data-cloud.log"
#define KSP_PREVIOUS STATE "/patches.ini.previous"
#define KSP_ABSENT STATE "/patches.absent"
#define KSP_DRY_RUN "/switch/openpak/save-data-cloud.dry-run"

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p, uint32_t v) {
    for (int i=0; i<4; ++i) p[i]=(uint8_t)(v>>(i*8));
}
static bool fingerprint(const void *data, size_t size, const char *expected) {
    uint8_t digest[32]; char hex[65]; hash(digest,data,size);
    for (int i=0;i<32;++i) snprintf(hex+i*2,3,"%02x",digest[i]);
    return strcmp(hex,expected)==0;
}

// package3 layout (fusee/build_package3.py): a content table of up to 30 entries at 0x40 (offset,
// size, name at +16); the KIPs back to back from 0x100000, 16-byte aligned with 0xCC between and
// after them, emummc first, then the stratosphere KIPs in table order; meta i at 0x400 (program
// id, offset from 0x100000, size, SHA-256) describes the i-th of them; fusee at 0x7C0000, zero
// padded to 0x20000.
#define KIP_START 0x100000
#define KIP_END 0x400000
#define FUSEE_AT 0x7C0000
#define FUSEE_MAX 0x20000
#define LOADER_ID "\x01\x00\x00\x00\x00\x00\x00\x01"   // 0x0100000000000001, little endian

static uint64_t kip_size(const uint8_t *k) { return 0x100+(uint64_t)get32(k+0x28)+get32(k+0x38)+get32(k+0x48); }
// The KIPs laid out as build_package3.py lays them out, into a 0x300000 region.
static bool lay_out(uint8_t *region,uint32_t count,const uint8_t *const *kip,const uint32_t *len,uint32_t *at) {
    memset(region,0xcc,KIP_END-KIP_START);
    uint32_t o=KIP_START;
    for (uint32_t i=0;i<count;++i) {
        if (len[i]>KIP_END-o) return false;
        at[i]=o; memcpy(region+o-KIP_START,kip[i],len[i]); o=(o+len[i]+15)&~15u;
    }
    return true;
}
// A replacement KIP keeps the header fields and capabilities of the one it replaces; only its
// segments may change.
static bool same_kip(const uint8_t *old,const uint8_t *k,size_t n) {
    return n>=0x100 && n<=0x100000 && kip_size(k)==n && !memcmp(old,k,0x20) && !memcmp(old+0x80,k+0x80,0x80);
}

bool openpak_package_build(uint8_t *p, size_t size, const uint8_t *kip, size_t n,
                           const uint8_t *loader, size_t ln, const uint8_t *fusee, size_t fn) {
    // Exact release match prevents altering an unsupported or independently modified boot image.
    if (size!=PACKAGE_SIZE || !fingerprint(p,size,
        "3cc9d6ca5688e403c36974bced1089397ef8b36a4252f720207ae3d41a6eb557")) return false;
    if (n<0x100 || n>0x100000 || memcmp(kip,"KIP1ams.mitm",12)) return false;
    if (!loader!=!fusee) return false;
    if (loader && (ln<0x100 || memcmp(loader,"KIP1Loader\0\0\0\0\0\0",16) || memcmp(loader+0x10,LOADER_ID,8))) return false;
    if (fusee && (fn<0x100 || fn>FUSEE_MAX)) return false;
    uint32_t count=get32(p+0x30), kips=get32(p+0x10), fh=0, found=0;
    if (count>30 || kips>15) return false;
    uint32_t h[16]={0}, at[16], len[16]; const uint8_t *data[16];
    for (uint32_t i=0,k=0;i<count;++i) {
        uint32_t e=0x40+i*32, off=get32(p+e), sz=get32(p+e+4);
        if (!memcmp(p+e+16,"fusee\0",6)) fh=e;
        if (off<KIP_START || off>=KIP_END) continue;
        if (k>kips || sz<0x100 || sz>KIP_END-off) return false;
        h[k]=e; data[k]=p+off; len[k++]=sz;
    }
    if (!h[0] || memcmp(p+h[0]+16,"emummc\0",7)) return false;
    for (uint32_t i=0;i<=kips;++i) {
        uint32_t m=0x400+i*48, rel=get32(p+m+8);
        if (!h[i] || rel!=get32(p+h[i])-KIP_START || get32(p+m+12)!=len[i] || memcmp(p+m,data[i]+0x10,8)) return false;
        uint8_t digest[32]; hash(digest,data[i],len[i]);
        if (memcmp(digest,p+m+16,32)) return false;
    }
    // fusee.bin opens with the loader stub's fixed entry code; the first 0x40 bytes identify it.
    if (fusee && (!fh || get32(p+fh)!=FUSEE_AT || get32(p+fh+4)>FUSEE_MAX || memcmp(p+FUSEE_AT,fusee,0x40))) return false;
    if (fusee) for (uint32_t i=get32(p+fh+4);i<FUSEE_MAX;++i) if (p[FUSEE_AT+i]) return false;
    uint8_t *region=malloc(KIP_END-KIP_START); if (!region) return false;
    // The package must already be laid out this way, so that repacking it moves nothing but
    // what was replaced.
    bool ok=lay_out(region,kips+1,data,len,at) && !memcmp(region,p+KIP_START,KIP_END-KIP_START);
    for (uint32_t i=1;ok && i<=kips;++i) {
        if (!memcmp(p+h[i]+16,"ams_mitm\0",9)) { ok=same_kip(data[i],kip,n); data[i]=kip; len[i]=n; found|=1; }
        else if (loader && !memcmp(p+h[i]+16,"Loader\0",7)) { ok=same_kip(data[i],loader,ln); data[i]=loader; len[i]=ln; found|=2; }
    }
    // Loader is not the last KIP and the new one is larger, so everything after it moves.
    ok=ok && found==(loader?3u:1u) && lay_out(region,kips+1,data,len,at);
    if (ok) {
        for (uint32_t i=0;i<=kips;++i) {
            uint32_t m=0x400+i*48;
            put32(p+h[i],at[i]); put32(p+h[i]+4,len[i]);
            put32(p+m+8,at[i]-KIP_START); put32(p+m+12,len[i]); hash(p+m+16,region+at[i]-KIP_START,len[i]);
        }
        memcpy(p+KIP_START,region,KIP_END-KIP_START);
        if (fusee) { memcpy(p+FUSEE_AT,fusee,fn); memset(p+FUSEE_AT+fn,0,FUSEE_MAX-fn); put32(p+fh+4,fn); }
    }
    free(region); return ok;
}

bool openpak_store_replace(const uint8_t *s, size_t size, const uint8_t *ca,
                          size_t ca_size, uint8_t **out, size_t *out_size) {
    if (size<8 || size>2*1024*1024 || !ca_size || ca_size>65536 || get32(s)!=0x546c7373) return false;
    uint32_t count=get32(s+4), target=0;
    if (count>(size-8)/16) return false;
    for (uint32_t i=0;i<count;++i) {
        uint32_t pos=8+i*16, id=get32(s+pos), len=get32(s+pos+8), off=get32(s+pos+12);
        if (off<count*16 || off>size-8 || len>size-8-off) return false;
        for (uint32_t j=0;j<i;++j) if (get32(s+8+j*16)==id) return false;
        if (id==1033) { if (get32(s+pos+4)!=1) return false; target=pos; }
    }
    if (!target) return false;
    size_t end=(size+3)&~(size_t)3;
    uint8_t *b=calloc(1,end+ca_size); if (!b) return false;
    memcpy(b,s,size); put32(b+target+8,ca_size); put32(b+target+12,end-8);
    memcpy(b+end,ca,ca_size); *out=b; *out_size=end+ca_size; return true;
}

// True when entry 1033 of a store already carries this CA: the file read back through LayeredFS
// is an OpenPak overlay, to be kept as it is rather than patched a second time.
bool openpak_store_is_ours(const uint8_t *s, size_t size, const uint8_t *ca, size_t ca_size) {
    if (size<8 || get32(s)!=0x546c7373) return false;
    uint32_t count=get32(s+4);
    if (count>(size-8)/16) return false;
    for (uint32_t i=0;i<count;++i) {
        uint32_t pos=8+i*16, id=get32(s+pos), len=get32(s+pos+8), off=get32(s+pos+12);
        if (id!=1033) continue;
        return len==ca_size && off<=size-8 && len<=size-8-off && !memcmp(s+8+off,ca,len);
    }
    return false;
}

// Change only standard package3/fss0 values. All other lines, including autoboot,
// retain their original bytes. The active package can stay open throughout setup.
bool openpak_boot_build(const uint8_t *source, size_t size, uint8_t **out, size_t *out_size) {
    if (!size || size > 65536 || memchr(source, 0, size)) return false;
    uint8_t *result = malloc(size * 2 + 16);
    if (!result) return false;
    size_t used = 0, changed = 0;
    for (size_t begin = 0; begin < size;) {
        size_t end = begin;
        while (end < size && source[end] != '\n') ++end;
        size_t next = end < size ? end + 1 : end;
        size_t key = begin;
        while (key < end && (source[key] == ' ' || source[key] == '\t')) ++key;
        size_t equals = key;
        while (equals < end && source[equals] != '=') ++equals;
        size_t key_end = equals;
        while (key_end > key && (source[key_end-1] == ' ' || source[key_end-1] == '\t')) --key_end;
        size_t value = equals < end ? equals + 1 : end;
        while (value < end && (source[value] == ' ' || source[value] == '\t')) ++value;
        size_t value_end = end;
        while (value_end > value && (source[value_end-1] == ' ' || source[value_end-1] == '\t' || source[value_end-1] == '\r')) --value_end;
        const char original[] = "atmosphere/package3";
        bool match = key_end-key == 4 && (!memcmp(source+key,"pkg3",4) || !memcmp(source+key,"fss0",4)) &&
                     value_end-value == sizeof(original)-1 && !memcmp(source+value,original,sizeof(original)-1);
        size_t prefix_end = match ? value_end : next;
        memcpy(result+used,source+begin,prefix_end-begin); used += prefix_end-begin;
        if (match) {
            memcpy(result+used,"-openpak",8); used += 8;
            memcpy(result+used,source+value_end,next-value_end); used += next-value_end;
            ++changed;
        }
        begin = next;
    }
    if (!changed) { free(result); return false; }
    *out = result; *out_size = used; return true;
}

static void path(char out[512],const char *p) { snprintf(out,512,"%s%s",openpak_root,p); }
static bool exists(const char *p) { char full[512]; path(full,p); struct stat st; return stat(full,&st)==0; }
static bool erase(const char *p) { char full[512]; path(full,p); return remove(full)==0 || errno==ENOENT; }
static bool move(const char *a,const char *b) { char x[512],y[512];path(x,a);path(y,b);return rename(x,y)==0; }
static uint8_t *read_file(const char *p,size_t *size,bool asset) {
    char full[512]; if (asset) snprintf(full,sizeof(full),"%s",p); else path(full,p);
    FILE *f=fopen(full,"rb"); if (!f) return NULL;
    if (fseek(f,0,SEEK_END)) { fclose(f);return NULL; }
    long n=ftell(f); if(n<0 || n>16*1024*1024 || fseek(f,0,SEEK_SET)) { fclose(f);return NULL; }
    uint8_t *b=malloc(n?n:1); if(!b) { fclose(f);return NULL; }
    bool ok=fread(b,1,n,f)==(size_t)n && !ferror(f); fclose(f);
    if(!ok) { free(b);return NULL; } *size=n;return b;
}
static bool same_file(const char *p,const void *data,size_t n) {
    char full[512];path(full,p);FILE *f=fopen(full,"rb");if(!f)return false;
    const uint8_t *expected=data;uint8_t buffer[4096];size_t offset=0;bool ok=true;
    while(offset<n) {
        size_t count=n-offset;if(count>sizeof(buffer))count=sizeof(buffer);
        if(fread(buffer,1,count,f)!=count || memcmp(buffer,expected+offset,count)) { ok=false;break; }
        offset+=count;
    }
    if(ok && (fgetc(f)!=EOF || ferror(f)))ok=false;
    if(fclose(f))ok=false;
    return ok;
}

static bool write_file(const char *p,const void *data,size_t n) {
    char full[512];path(full,p);
    for(char *c=full+1;*c;++c) if(*c=='/') { *c=0;mkdir(full,0777);*c='/'; }
    FILE *f=fopen(full,"wb");if(!f)return false;
    bool ok=fwrite(data,1,n,f)==n; if(fflush(f))ok=false;if(fclose(f))ok=false;
    return ok && same_file(p,data,n);
}
static bool recover_file(const char *p) {
    char previous[480];snprintf(previous,sizeof(previous),"%s.openpak-previous",p);
    return exists(p) || !exists(previous) || move(previous,p);
}
static bool replace_file(const char *p,const void *data,size_t n) {
    if (same_file(p,data,n)) return true;
    char temp[480],previous[480];snprintf(temp,sizeof(temp),"%s.openpak-new",p);snprintf(previous,sizeof(previous),"%s.openpak-previous",p);
    // Recover an interrupted activation before starting another one.
    if(exists(previous)) {
        if(!exists(p)) { if(!move(previous,p))return false; }
        else if(!erase(previous))return false;
    }
    if(!write_file(temp,data,n))return false;
    bool had=exists(p);if(had && !move(p,previous))return false;
    if(!move(temp,p)) { if(had)move(previous,p);return false; }
    if(!same_file(p,data,n))return false;
    return erase(previous);
}
static bool backup(const char *p,const char *saved,const char *absent) {
    if(exists(saved)||exists(absent))return true;
    if(!exists(p))return write_file(absent,"",0);
    size_t n=0;uint8_t *b=read_file(p,&n,false);if(!b)return false;
    bool ok=write_file(saved,b,n);free(b);return ok;
}
static bool restore(const char *p,const char *saved,const char *absent) {
    if(exists(saved)) {
        size_t n=0;uint8_t *b=read_file(saved,&n,false);if(!b)return false;
        bool ok=replace_file(p,b,n);free(b);return ok;
    }
    return !exists(absent) || erase(p);
}

static uint8_t *system_store(size_t *size) {
#ifdef OPENPAK_HOST_TEST
    return read_file(STATE "/source.bdf",size,false);
#else
    // The file as the ssl service reads it: 127 entries on 22.5.0, original layout. The live list
    // below (sslGetCertificates) exposes only 63 of them and re-lays the file out; it stays as the
    // fallback. LayeredFS applies to this mount too, so an active overlay comes back unchanged and
    // the caller keeps it (openpak_store_is_ours) instead of appending the CA again.
    if(R_SUCCEEDED(romfsMountFromDataArchive(0x0100000000000800ULL,NcmStorageId_BuiltInSystem,"certstore"))) {
        uint8_t *file=read_file("certstore:/ssl_TrustedCerts.bdf",size,true);
        romfsUnmount("certstore");
        if(file && *size>=8 && get32(file)==0x546c7373)return file;
        free(file);
    }
    if(R_FAILED(sslInitialize(1)))return NULL;
    u32 id=SslCaCertificateId_All,n=0,total=0;
    Result rc=sslGetCertificateBufSize(&id,1,&n);
    void *raw=NULL;uint8_t *b=NULL;
    if(R_SUCCEEDED(rc) && n && n<=2*1024*1024) {
        raw=calloc(1,n);
        if(raw)rc=sslGetCertificates(raw,n,&id,1,&total);
        if(raw && R_SUCCEEDED(rc) && total && total<=n/sizeof(SslBuiltInCertificateInfo)) {
            SslBuiltInCertificateInfo *info=raw;size_t bytes=8+total*16;bool ok=true;
            for(u32 i=0;i<total;++i) {
                uintptr_t off=(uintptr_t)info[i].cert_data-(uintptr_t)raw;
                if(info[i].cert_size && (off>n || info[i].cert_size>n-off)) { ok=false;break; }
                bytes+=(info[i].cert_size+3)&~(size_t)3;
            }
            if(ok && bytes<=2*1024*1024) b=calloc(1,bytes);
            if(b) {
                put32(b,0x546c7373);put32(b+4,total);size_t off=total*16;
                for(u32 i=0;i<total;++i) {
                    uint8_t *e=b+8+i*16;put32(e,info[i].cert_id);put32(e+4,info[i].status);
                    put32(e+8,info[i].cert_size);put32(e+12,off);
                    if(info[i].cert_size)memcpy(b+8+off,info[i].cert_data,info[i].cert_size);
                    off+=(info[i].cert_size+3)&~(size_t)3;
                }
                *size=bytes;
            }
        }
    }
    free(raw);sslExit();return b;
#endif
}

static bool managed_or_original(const char *p,const char *managed,const char *original,const char *absent);

// An overlay carrying our CA in slot 1033 is ours whatever its layout: one an older build wrote
// from the live certificate list (63 of the 127 entries, re-laid out), or the same file read back
// through LayeredFS. Re-applying OpenPak may overwrite it. A file that is not there has nothing
// to preserve either. Only a store somebody else put here is kept, and refused.
bool openpak_store_file_is_ours(void) {
    if(!exists(STORE))return true;
    size_t n=0,cn=0;bool ours=false;
    uint8_t *b=read_file(STORE,&n,false),*ca=read_file(ASSET "ca.der",&cn,true);
    if(b&&ca)ours=openpak_store_is_ours(b,n,ca,cn);
    free(b);free(ca);return ours;
}


// exosphere.ini's blank_prodinfo_emummc: 1 hides the console's identity from Nintendo (Prelude's
// "Nintendo" mode sets it), but a blank identity has no device certificate, so under OpenPak
// nn.account fails 2123-0011 before opening a connection and nothing signs in (2026-09-24
// 21:35Z). Under OpenPak's redirects the real identity never reaches Nintendo, so enable writes
// 0; disable writes 1, the safe state for a console about to talk to Nintendo again. The file as
// found is kept in /switch/openpak/system/exosphere.previous. blank_prodinfo_sysmmc is untouched.
bool openpak_exosphere_blank(bool blank,char *err,int errlen) {
    static const char key[]="blank_prodinfo_emummc=";const size_t kl=sizeof(key)-1;
    const char want=blank?'1':'0';
    size_t n=0;uint8_t *b=read_file(EXOSPHERE,&n,false);
    if(!b && !blank)return true;                          // no file: exosphere defaults to 0
    uint8_t *out=malloc(n+64);if(!out) { free(b);snprintf(err,errlen,"out of memory");return false; }
    size_t o=0;long after_header=-1;bool seen=false,changed=false;
    for(size_t i=0;i<n;) {
        size_t e=i;while(e<n && b[e]!='\n')++e;size_t next=e<n?e+1:e;   // one line, newline included
        if(e-i>kl && !memcmp(b+i,key,kl)) {
            if(seen) { changed=true;i=next;continue; }    // a duplicate line: dropped
            memcpy(out+o,b+i,next-i);
            if(out[o+kl]!=want) { out[o+kl]=want;changed=true; }
            o+=next-i;seen=true;i=next;continue;
        }
        memcpy(out+o,b+i,next-i);o+=next-i;
        if(after_header<0 && e-i>=11 && !memcmp(b+i,"[exosphere]",11)) {
            if(out[o-1]!='\n')out[o++]='\n';
            after_header=(long)o;
        }
        i=next;
    }
    if(!seen) {                                           // key missing: add it under [exosphere]
        char line[40];int ll=snprintf(line,sizeof(line),"%s%c\n",key,want);
        if(after_header<0) {
            if(o && out[o-1]!='\n')out[o++]='\n';
            memcpy(out+o,"[exosphere]\n",12);o+=12;after_header=(long)o;
        }
        memmove(out+after_header+ll,out+after_header,o-(size_t)after_header);
        memcpy(out+after_header,line,(size_t)ll);o+=(size_t)ll;changed=true;
    }
    bool ok=true;
    if(changed) {
        if(b && !write_file(STATE "/exosphere.previous",b,n)) { snprintf(err,errlen,"Could not back up exosphere.ini");ok=false; }
        else if(!write_file(EXOSPHERE,out,o)) { snprintf(err,errlen,"Could not update exosphere.ini");ok=false; }
    }
    free(b);free(out);return ok;
}

// Save Data Cloud (docs/save-data-cloud.md): hekate swaps the FS key-seed-package key at boot from
// bootloader/patches.ini, for the launch entries that ask with kip1patch=openpak_ksp. patches.ini is
// written before an entry asks and cleaned only once none does: hekate stops at boot ("Failed to
// apply") when an entry asks for a set the file does not hold. hekate_ipl.ini's request rides in the
// managed boot entry, so selecting Nintendo restores it with the rest of boot.original.
// /switch/openpak/save-data-cloud.dry-run on the card: work it all out, log it, write nothing.
#ifdef OPENPAK_HOST_TEST
char openpak_test_firmware[16]="";     // the host test's console
bool openpak_test_emummc=true;
static void firmware(char *v,int n) { snprintf(v,n,"%s",openpak_test_firmware); }
static int on_emummc(void) { return openpak_test_emummc; }
#else
static void firmware(char *v,int n) { openpak_firmware_supported(v,n); }
// Exosphère's ExosphereEmummcType (spl config 65007): non-zero while this boot runs on emuMMC.
static int on_emummc(void) {
    u64 v=0;
    if(R_FAILED(splInitialize()))return -1;
    Result rc=splGetConfig((SplConfigItem)65007,&v);
    splExit();
    return R_SUCCEEDED(rc)?v!=0:-1;
}
#endif
static char ksp_note[160];
const char *openpak_ksp_note(void) { return ksp_note; }

#define KSP_LOG_SIZE 16384
static void ksp_describe(char *log,const char *file,const uint8_t *a,size_t an,const uint8_t *b,size_t bn) {
    size_t at=strlen(log);
    if(at+64>=KSP_LOG_SIZE)return;
    at+=snprintf(log+at,KSP_LOG_SIZE-at,"%s:\n",file);
    size_t w=openpak_ksp_describe(a,an,b,bn,log+at,KSP_LOG_SIZE-at);
    if(!w)snprintf(log+at,KSP_LOG_SIZE-at,"  (no change)\n");
}
static void ksp_finish(char *log) {
    size_t at=strlen(log);
    snprintf(log+at,KSP_LOG_SIZE-at,"result: %s\n",ksp_note[0]?ksp_note:"done");
    write_file(KSP_LOG,log,strlen(log));
    free(log);
}

// Enable: patches.ini gets the two [FS:…] sections, *boot (the managed hekate_ipl.ini about to be
// written) the request. True when both are so.
static bool ksp_install(uint8_t **boot,size_t *boot_size) {
    char fw[32]="";firmware(fw,sizeof(fw));
    char *log=calloc(1,KSP_LOG_SIZE);if(!log) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud: out of memory.");return false; }
    bool dry=exists(KSP_DRY_RUN),ok=false;
    int emummc=on_emummc();
    size_t en=0,pn=0,nn=0,bn=0;
    uint8_t *emu=read_file(EMUMMC_INI,&en,false),*patches=NULL,*new_patches=NULL,*new_boot=NULL;
    bool emu_enabled=openpak_ksp_emummc_enabled(emu,en);
    snprintf(log,KSP_LOG_SIZE,"OpenPak Save Data Cloud: enable%s\nfirmware %s, running on %s, emuMMC/emummc.ini %s\n",
             dry?" (dry run, nothing written)":"",fw[0]?fw:"unknown",emummc<0?"unknown":emummc?"emuMMC":"sysMMC",
             emu_enabled?"enabled":"disabled or absent");
    if(!openpak_ksp_firmware(fw)) {
        snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud needs firmware " OPENPAK_KSP_FIRMWARE "; this console runs %s.",fw[0]?fw:"another");
        goto done;
    }
    if(emummc<0) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud not set up: could not tell emuMMC from sysMMC.");goto done; }
    int r=openpak_ksp_boot_build(*boot,*boot_size,true,emummc,emu_enabled,&new_boot,&bn);
    if(r<0) {
        snprintf(ksp_note,sizeof(ksp_note),r==-2?"Save Data Cloud: out of memory.":
                 "Save Data Cloud not set up: no hekate entry boots this %s through Atmosphere.",emummc?"emuMMC":"sysMMC");
        goto done;
    }
    if(!recover_file(PATCHES)) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud not set up: could not recover patches.ini.");goto done; }
    bool present=exists(PATCHES);
    patches=present?read_file(PATCHES,&pn,false):NULL;
    if(present && !patches) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud not set up: could not read bootloader/patches.ini.");goto done; }
    int p=openpak_ksp_patches_build(patches,pn,true,&new_patches,&nn);
    if(p<0) {
        snprintf(ksp_note,sizeof(ksp_note),p==-2?"Save Data Cloud: out of memory.":
                 "Save Data Cloud not set up: bootloader/patches.ini is not one OpenPak can edit; left as it is.");
        goto done;
    }
    ksp_describe(log,"bootloader/patches.ini",patches,pn,p?new_patches:patches,p?nn:pn);
    ksp_describe(log,"bootloader/hekate_ipl.ini",*boot,*boot_size,r?new_boot:*boot,r?bn:*boot_size);
    if(dry) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud dry run: see /switch/openpak/system/save-data-cloud.log");goto done; }
    if(p) {
        // The file as it was before this edit; no file: deleting ours later deletes the file.
        if(present?!write_file(KSP_PREVIOUS,patches,pn):!write_file(KSP_ABSENT,"",0)) {
            snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud not set up: could not back up patches.ini.");goto done;
        }
        if(!replace_file(PATCHES,new_patches,nn)) {
            snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud not set up: could not write bootloader/patches.ini.");goto done;
        }
    }
    if(r) { free(*boot);*boot=new_boot;*boot_size=bn;new_boot=NULL; }
    ok=true;
done:
    ksp_finish(log);
    free(emu);free(patches);free(new_patches);free(new_boot);
    return ok;
}

// Nintendo: OpenPak's sections leave patches.ini, once hekate_ipl.ini no longer asks for them.
static void ksp_remove(void) {
    if(!recover_file(PATCHES)) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud: could not recover patches.ini.");return; }
    size_t pn=0,nn=0,bn=0,xn=0;
    uint8_t *patches=read_file(PATCHES,&pn,false),*new_patches=NULL,*boot=NULL,*x=NULL;
    int p=patches?openpak_ksp_patches_build(patches,pn,false,&new_patches,&nn):0;
    if(!p) { free(patches);if(!exists(PATCHES))erase(KSP_ABSENT);return; }   // nothing of ours there
    char *log=calloc(1,KSP_LOG_SIZE);
    if(!log) { free(patches);free(new_patches);return; }
    bool dry=exists(KSP_DRY_RUN);
    snprintf(log,KSP_LOG_SIZE,"OpenPak Save Data Cloud: remove%s\n",dry?" (dry run, nothing written)":"");
    if(p<0) {
        snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud: bootloader/patches.ini is not one OpenPak can edit; left as it is.");
        goto done;
    }
    ksp_describe(log,"bootloader/patches.ini",patches,pn,new_patches,nn);
    boot=read_file(BOOT,&bn,false);
    if(boot && openpak_ksp_boot_build(boot,bn,false,false,false,&x,&xn)!=0) {
        snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud kept: bootloader/hekate_ipl.ini still asks for " OPENPAK_KSP_PATCH ".");
        goto done;
    }
    if(dry) { snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud dry run: see /switch/openpak/system/save-data-cloud.log");goto done; }
    if(!write_file(KSP_PREVIOUS,patches,pn) ||
       !(nn==0 && exists(KSP_ABSENT)?erase(PATCHES):replace_file(PATCHES,new_patches,nn))) {
        snprintf(ksp_note,sizeof(ksp_note),"Save Data Cloud: could not update bootloader/patches.ini.");
        goto done;
    }
    erase(KSP_ABSENT);
done:
    ksp_finish(log);
    free(patches);free(new_patches);free(boot);free(x);
}

// The packages earlier builds wrote. They are OpenPak's own, so a rebuild may replace them.
// 2: from Atmosphere 1.11.2, ams_mitm alone (0.3.1-0.3.14) or with store trust (0.3.15-0.3.17).
// 1: from Atmosphere 1.12.0 without the firmware-23 dns.mitm fix (0.3.18-0.3.19).
int openpak_package_outdated(void) {
    size_t n=0;uint8_t *b=read_file(OPENPAK_PACKAGE,&n,false);if(!b)return 0;
    int age=fingerprint(b,n,"5567550fc47a48547f169615fbebc9a6cb702af45bab549b31e1bf678447c467") ||
            fingerprint(b,n,"4e2ac49bb8547c8d17af1ebcc092d60a3c4932a7c31ee2e2bd06113e619530e2") ? 2 :
            fingerprint(b,n,"12f5eb5225d42ba1f08b285c4b07956c66d32e57b871874b6e5b222c2934b41a") ||
            fingerprint(b,n,"eff03265fe175feb424c0d7e42726b13f5ef8eb93824e91ce1556f69175815ca") ? 1 : 0;
    free(b);return age;
}

bool openpak_system_install(char *err,int errlen) {
    ksp_note[0]='\0';
    if(!recover_file(BOOT) || !recover_file(STORE) || !recover_file(OPENPAK_PACKAGE)) {
        snprintf(err,errlen,"Could not recover interrupted system setup");return false;
    }
    bool ok=false;
    uint8_t *base=NULL,*kip=NULL,*loader=NULL,*fusee=NULL,*previous=NULL,*source=NULL,*ca=NULL,*store=NULL,*boot=NULL,*new_boot=NULL;
    size_t n=0,kn=0,ln=0,fn=0,sn=0,cn=0,outn=0,bn=0,new_bn=0;
    const char *failure="Could not prepare system support";
    base=read_file(PACKAGE,&n,false);
    kip=read_file(ASSET "system/ams_mitm-1.12.0.kip",&kn,true);
    loader=read_file(ASSET "system/loader-1.12.0.kip",&ln,true);
    fusee=read_file(ASSET "system/fusee-1.12.0.bin",&fn,true);
    if(!base||!kip||!loader||!fusee||!(previous=malloc(n?n:1)))goto done;
    memcpy(previous,base,n);
    // The package this one replaces: ams_mitm alone, what every build before store trust wrote
    // to /atmosphere/package3-openpak. Built from the same official package to recognise it.
    failure="System support requires unmodified Atmosphere 1.12.0";
    if(!openpak_package_build(base,n,kip,kn,loader,ln,fusee,fn) ||
       !openpak_package_build(previous,n,kip,kn,NULL,0,NULL,0))goto done;
    failure="Boot configuration changed; existing file preserved";
    if(!managed_or_original(BOOT,STATE "/boot.managed",STATE "/boot.original",STATE "/boot.absent"))goto done;
    boot=read_file(exists(STATE "/boot.original")?STATE "/boot.original":BOOT,&bn,false);
    failure="No standard Hekate package3 entry found";
    if(!boot || !openpak_boot_build(boot,bn,&new_boot,&new_bn))goto done;
    failure="Another tool's certificate overlay is in place; it was left alone";
    if(!managed_or_original(STORE,STATE "/certificate.managed",STATE "/certificate.original",STATE "/certificate.absent")
       && !openpak_store_file_is_ours())goto done;
    failure="OpenPak boot package changed; existing file preserved";
    if(exists(OPENPAK_PACKAGE) && !same_file(OPENPAK_PACKAGE,base,n) && !same_file(OPENPAK_PACKAGE,previous,n) &&
       !openpak_package_outdated())goto done;
    failure="Could not read the system certificates";
    source=system_store(&sn);ca=read_file(ASSET "ca.der",&cn,true);
    if(!source||!ca)goto done;
    if(openpak_store_is_ours(source,sn,ca,cn)) { store=source;outn=sn;source=NULL; }
    else if(!openpak_store_replace(source,sn,ca,cn,&store,&outn))goto done;
    failure="Could not clear blank_prodinfo_emummc in exosphere.ini";
    if(!openpak_exosphere_blank(false,err,errlen))goto done;
    failure="Could not back up system setup";
    // Our own overlay is never recorded as the file that was here first. Without this, a console
    // whose /switch/openpak/system folder was deleted while OpenPak was on would back the overlay
    // up as the original, and selecting Nintendo would restore it — leaving the OpenPak CA
    // trusted by a console that asked to go back.
    if(!exists(STATE "/certificate.original") && !exists(STATE "/certificate.absent") &&
       exists(STORE) && openpak_store_file_is_ours() &&
       !write_file(STATE "/certificate.absent","",0))goto done;
    if(!backup(BOOT,STATE "/boot.original",STATE "/boot.absent") ||
       !backup(STORE,STATE "/certificate.original",STATE "/certificate.absent"))goto done;
    // Never fails the switch: without it only Save Data Cloud is missing, and ksp_note says why.
    ksp_install(&new_boot,&new_bn);
    failure="Could not save OpenPak boot package";
    if(!replace_file(OPENPAK_PACKAGE,base,n))goto done;
    failure="Could not record system setup";
    if(!write_file(STATE "/boot.managed",new_boot,new_bn) || !write_file(STATE "/certificate.managed",store,outn))goto done;
    failure="Could not save the certificate overlay";
    if(!replace_file(STORE,store,outn) || !erase(CACHE))goto done;
    failure="Could not select the OpenPak boot package";
    if(!replace_file(BOOT,new_boot,new_bn)) {
        restore(STORE,STATE "/certificate.original",STATE "/certificate.absent");goto done;
    }
    ok=true;
done:
    free(base);free(kip);free(loader);free(fusee);free(previous);free(source);free(ca);free(store);free(boot);free(new_boot);
    if(!ok)snprintf(err,errlen,"%s",failure);
    return ok;
}

// fusee loads /atmosphere/kips before package3 and keeps the first KIP of each program id, so a
// Loader there (Horizon OC's hoc.kip is one) replaces the one OpenPak puts in its package, and
// store titles stop launching. Same test as fusee: a .kip or .kip1 name, KIP1, and a file size
// that matches the header. The file is only named, never touched.
bool openpak_loader_override(char *name,int len) {
    char dir[512];path(dir,"/atmosphere/kips");
    DIR *d=opendir(dir);if(!d)return false;
    bool found=false;
    for(struct dirent *e;!found && (e=readdir(d));) {
        size_t nl=strlen(e->d_name);
        if(!(nl>=4 && !strcmp(e->d_name+nl-4,".kip")) && !(nl>=5 && !strcmp(e->d_name+nl-5,".kip1")))continue;
        char full[800];snprintf(full,sizeof(full),"%s/%s",dir,e->d_name);
        FILE *f=fopen(full,"rb");if(!f)continue;
        uint8_t k[0x100];struct stat st;
        found=fread(k,1,sizeof(k),f)==sizeof(k) && !stat(full,&st) && S_ISREG(st.st_mode) && !memcmp(k,"KIP1",4) &&
              !memcmp(k+0x10,LOADER_ID,8) && (uint64_t)st.st_size==kip_size(k);
        fclose(f);
        if(found)snprintf(name,len,"%s",e->d_name);
    }
    closedir(d);return found;
}

static bool managed_or_original(const char *p,const char *managed,const char *original,const char *absent) {
    if(!exists(managed) && !exists(original) && !exists(absent))return true;
    if(!exists(p))return exists(absent);
    size_t n=0;uint8_t *b=read_file(p,&n,false);if(!b)return false;
    bool ok=same_file(managed,b,n)||same_file(original,b,n);free(b);return ok;
}
static bool system_remove(char *err,int errlen) {
    if(!recover_file(BOOT) || !recover_file(STORE)) {
        snprintf(err,errlen,"Could not recover interrupted system setup");return false;
    }
    if(!exists(STATE "/boot.original") && !exists(STATE "/certificate.original") && !exists(STATE "/certificate.absent"))return true;
    if(!managed_or_original(BOOT,STATE "/boot.managed",STATE "/boot.original",STATE "/boot.absent") ||
       (!managed_or_original(STORE,STATE "/certificate.managed",STATE "/certificate.original",STATE "/certificate.absent")
        && !openpak_store_file_is_ours())) {
        snprintf(err,errlen,"System files changed; originals kept in /switch/openpak/system");return false;
    }
    if(!restore(BOOT,STATE "/boot.original",STATE "/boot.absent") ||
       !restore(STORE,STATE "/certificate.original",STATE "/certificate.absent") || !erase(CACHE)) {
        snprintf(err,errlen,"Could not restore system setup; originals kept");return false;
    }
    // Keep the now-inactive package: it may still be open by the current boot.
    // It is no longer selected and an identical file can be reused on re-enable.
    const char *files[]={"boot.original","boot.absent","boot.managed","certificate.original","certificate.absent","certificate.managed"};
    for(size_t i=0;i<sizeof(files)/sizeof(files[0]);++i) { char p[256];snprintf(p,sizeof(p),STATE "/%s",files[i]);if(!erase(p)) { snprintf(err,errlen,"Restored system; could not remove backup metadata");return false; } }
    return openpak_exosphere_blank(true,err,errlen);
}
bool openpak_system_remove(char *err,int errlen) {
    ksp_note[0]='\0';
    if(!system_remove(err,errlen))return false;
    // After hekate_ipl.ini is back: hekate must never be asked for a set patches.ini no longer has.
    ksp_remove();
    return true;
}

// /atmosphere/config/override_config.ini, [hbl_config]: which programs Atmosphère's Loader swaps
// for the Homebrew Menu (libstratosphere cfg_override.board.nintendo_nx.inc, 1.11.2). Slot 0 is
// the Album (010000000000100D) with override_key_0=!R unless the file says otherwise: hbmenu
// unless R is held. "R" turns that round, hbmenu only while R is held, so the Album opens the
// Album. The Loader rereads the file at every launch, the way inih reads it: ';' and '#' start a
// comment line and a ';' after whitespace an inline one, '=' or ':' separates, sections and
// names in any case, an indented line continues the previous value, the last value wins, and
// program_id and override_key are slot 0's too. No file is all defaults.
#define OVERRIDE "/atmosphere/config/override_config.ini"
#define ALBUM 0x010000000000100DULL
static const char album_file[]="[hbl_config]\nprogram_id_0=010000000000100D\noverride_key_0=R\n";

static bool ini_space(uint8_t c) { return c==' '||c=='\t'||c=='\r'||c=='\v'||c=='\f'; }
static bool ini_name(const uint8_t *s,size_t n,const char *name) { return n==strlen(name) && !strncasecmp((const char *)s,name,n); }
// inih's find_chars_or_comment: the first of chars, or a ';' that follows whitespace.
static size_t ini_find(const uint8_t *s,size_t at,size_t end,const char *chars) {
    bool was_space=false;
    for(;at<end && !(chars && strchr(chars,s[at])) && !(was_space && s[at]==';');++at)was_space=ini_space(s[at]);
    return at;
}

int openpak_album_build(const uint8_t *s,size_t n,uint8_t **out,size_t *out_size) {
    const char *add=album_file;size_t at=0,cut=0;char line[64];   // add goes in at `at`, over cut bytes
    if(!s)n=0;
    else {
        if(n>65536 || memchr(s,0,n))return -1;
        unsigned long long slot[8]={ALBUM};
        bool hbl=false,first=false,seen=false,have_key=false;
        size_t key_at=0,key_end=0,insert=0,prev_at=0,prev_len=0;   // prev: the name a continuation extends
        for(size_t i=0;i<n;) {
            size_t b=i,e=i;while(e<n && s[e]!='\n')++e;size_t next=e<n?e+1:e;i=next;
            if(b==0 && n>=3 && !memcmp(s,"\xEF\xBB\xBF",3))b=3;
            size_t ve=e;while(ve>b && ini_space(s[ve-1]))--ve;
            size_t st=b;while(st<ve && ini_space(s[st]))++st;
            if(st==ve)continue;                                   // blank
            size_t name=prev_at,name_len=prev_len,vs=st;
            if(s[st]==';' || s[st]=='#') name_len=0;
            else if(prev_len && st>b) {}                          // continuation: the whole line is the value
            else if(s[st]=='[') {
                size_t c=ini_find(s,st+1,ve,"]");
                if(c<ve && s[c]==']') {
                    hbl=ini_name(s+st+1,c-st-1,"hbl_config");prev_len=0;
                    first=hbl && !seen;seen|=hbl;
                }
                name_len=0;
            } else {
                size_t sep=ini_find(s,st,ve,"=:");
                if(sep<ve && (s[sep]=='=' || s[sep]==':')) {
                    name=prev_at=st;name_len=sep-st;while(name_len && ini_space(s[st+name_len-1]))--name_len;
                    prev_len=name_len;
                    vs=sep+1;ve=ini_find(s,vs,ve,NULL);
                    while(vs<ve && ini_space(s[vs]))++vs;
                    while(ve>vs && ini_space(s[ve-1]))--ve;
                } else name_len=0;                                // inih skips the line
            }
            // The end of the first [hbl_config], blank lines aside: where a missing key goes.
            if(first)insert=next;
            if(!hbl || !name_len)continue;
            char value[64];snprintf(value,sizeof(value),"%.*s",(int)(ve-vs),(const char *)s+vs);
            const uint8_t *k=s+name;
            if(ini_name(k,name_len,"program_id"))slot[0]=strtoull(value,NULL,16);
            else if(name_len==12 && !strncasecmp((const char *)k,"program_id_",11) && k[11]>='0' && k[11]<='7')
                slot[k[11]-'0']=strtoull(value,NULL,16);
            else if(ini_name(k,name_len,"override_key") || ini_name(k,name_len,"override_key_0")) {
                have_key=true;key_at=vs;key_end=ve;
            }
        }
        // Somebody chose what slot 0 overrides, or sends the Album to hbmenu from another slot.
        if(slot[0]!=ALBUM)return -1;
        for(int i=1;i<8;++i) if(slot[i]==ALBUM)return -1;
        char key[8]="!R";
        if(have_key) {
            if(key_end-key_at>=sizeof(key))return -1;
            memcpy(key,s+key_at,key_end-key_at);key[key_end-key_at]=0;
        }
        if(!strcasecmp(key,"R"))return 0;
        if(strcasecmp(key,"!R"))return -1;                        // a key somebody else chose
        if(have_key) { at=key_at;cut=key_end-key_at;add="R"; }
        else {
            at=seen?insert:n;
            // The next line that is not blank or a comment must not become more of our value.
            for(size_t j=at;j<n;) {
                size_t t=j;while(t<n && ini_space(s[t]))++t;
                if(t<n && s[t]!='\n' && s[t]!=';' && s[t]!='#') { if(t>j)return -1;break; }
                while(t<n && s[t]!='\n')++t;
                j=t+1;
            }
            const uint8_t *nl=memchr(s,'\n',n);
            const char *eol=nl && nl>s && nl[-1]=='\r'?"\r\n":"\n";
            snprintf(line,sizeof(line),"%s%s%soverride_key_0=R%s",at && s[at-1]!='\n'?eol:"",
                     seen?"":"[hbl_config]",seen?"":eol,eol);
            add=line;
        }
    }
    size_t al=strlen(add),size=n-cut+al;
    uint8_t *b=malloc(size);if(!b)return -2;
    if(at)memcpy(b,s,at);
    memcpy(b+at,add,al);
    if(n>at+cut)memcpy(b+at+al,s+at+cut,n-at-cut);
    *out=b;*out_size=size;return 1;
}

#define ALBUM_MANAGED STATE "/album.managed"
#define ALBUM_ORIGINAL STATE "/album.original"
#define ALBUM_ABSENT STATE "/album.absent"
static bool album_tracked(void) { return exists(ALBUM_MANAGED) || exists(ALBUM_ORIGINAL) || exists(ALBUM_ABSENT); }

bool openpak_album_install(char *note,int len) {
    note[0]='\0';
    if(!recover_file(OVERRIDE)) { snprintf(note,len,"Could not recover override_config.ini."); return false; }
    bool ok=false,present=exists(OVERRIDE);
    size_t n=0,sn=0,on=0;uint8_t *file=present?read_file(OVERRIDE,&n,false):NULL,*saved=NULL,*out=NULL;
    const uint8_t *from=file,*want;size_t fn=n,wn;int r;
    const char *failure="Could not read override_config.ini.";
    if(present && !file)goto done;
    failure="Could not record the Album setup.";
    if(!album_tracked()) {
        // A file exactly as OpenPak creates it is OpenPak's, also with the record of it gone: no
        // file was there first, so disable deletes it.
        if(file && n==sizeof(album_file)-1 && !memcmp(file,album_file,n) &&
           (!write_file(ALBUM_ABSENT,"",0) || !write_file(ALBUM_MANAGED,file,n)))goto done;
    } else if(!managed_or_original(OVERRIDE,ALBUM_MANAGED,ALBUM_ORIGINAL,ALBUM_ABSENT)) {
        // Changed since OpenPak edited it: whoever changed it keeps it.
        ok=file && openpak_album_build(file,n,&out,&on)==0;
        failure="Album left as it is: override_config.ini changed after OpenPak edited it.";
        goto done;
    }
    // Built from the file that was here first, as hekate_ipl.ini is.
    failure="Could not read the saved override_config.ini.";
    if(exists(ALBUM_ORIGINAL)) { if(!(saved=read_file(ALBUM_ORIGINAL,&sn,false)))goto done;from=saved;fn=sn; }
    else if(exists(ALBUM_ABSENT)) { from=NULL;fn=0; }
    r=openpak_album_build(from,fn,&out,&on);
    failure=r==-2?"Out of memory.":"Album left as it is: override_config.ini has its own Homebrew Menu setup.";
    if(r<0)goto done;
    if(r==0 && !album_tracked()) { ok=true;goto done; }         // already so, and the user's: nothing to record
    want=r?out:from;wn=r?on:fn;
    failure="Could not back up override_config.ini.";
    if(!backup(OVERRIDE,ALBUM_ORIGINAL,ALBUM_ABSENT))goto done;
    failure="Could not record the Album setup.";
    if(!write_file(ALBUM_MANAGED,want,wn))goto done;
    failure="Could not update override_config.ini.";
    ok=replace_file(OVERRIDE,want,wn);
done:
    if(!ok)snprintf(note,len,"%s",failure);
    free(file);free(saved);free(out);return ok;
}

bool openpak_album_remove(char *note,int len) {
    note[0]='\0';
    if(!recover_file(OVERRIDE)) { snprintf(note,len,"Could not recover override_config.ini."); return false; }
    if(!album_tracked())return true;
    if(!managed_or_original(OVERRIDE,ALBUM_MANAGED,ALBUM_ORIGINAL,ALBUM_ABSENT)) {
        snprintf(note,len,"override_config.ini changed after OpenPak edited it; left as it is.");return false;
    }
    if(!restore(OVERRIDE,ALBUM_ORIGINAL,ALBUM_ABSENT)) { snprintf(note,len,"Could not restore override_config.ini.");return false; }
    if(!erase(ALBUM_MANAGED) || !erase(ALBUM_ORIGINAL) || !erase(ALBUM_ABSENT)) {
        snprintf(note,len,"Restored override_config.ini; could not remove its backup.");return false;
    }
    return true;
}

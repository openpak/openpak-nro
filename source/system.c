// SPDX-License-Identifier: AGPL-3.0-only
#include "system.h"
#include "hosts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef OPENPAK_HOST_TEST
#include <openssl/sha.h>
#define ASSET "romfs/"
static void hash(uint8_t out[32], const void *data, size_t n) { SHA256(data, n, out); }
#else
#include <switch.h>
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

bool openpak_package_build(uint8_t *p, size_t size, const uint8_t *kip, size_t n) {
    // Exact release match prevents altering an unsupported or independently modified boot image.
    if (size!=PACKAGE_SIZE || !fingerprint(p,size,
        "f162a419887374028103e097dc5679f97b3b22501fee667405a3bc965eeaa3f2")) return false;
    if (n<0x100 || n>0x100000 || memcmp(kip,"KIP1ams.mitm",12)) return false;
    uint32_t count=get32(p+0x30), header=0, offset=0, old_size=0, meta=0;
    if (count>30) return false;
    for (uint32_t i=0;i<count;++i) {
        uint32_t h=0x40+i*32;
        if (!memcmp(p+h+16,"ams_mitm\0",9)) {
            if (i!=count-1) return false;
            header=h; offset=get32(p+h); old_size=get32(p+h+4);
        }
    }
    if (!header || offset<0x100000 || offset>0x400000 || old_size>0x400000-offset || n>0x400000-offset) return false;
    if (memcmp(p+offset,kip,32) || memcmp(p+offset+0x80,kip+0x80,0x80)) return false;
    uint32_t kips=get32(p+0x10);
    if (kips>15) return false;
    for (uint32_t i=0;i<=kips;++i) {
        uint32_t m=0x400+i*48, rel=get32(p+m+8), len=get32(p+m+12);
        if (rel>0x300000 || len>0x300000-rel) return false;
        uint8_t digest[32]; hash(digest,p+0x100000+rel,len);
        if (memcmp(digest,p+m+16,32)) return false;
        if (rel+0x100000==offset) meta=m;
    }
    if (!meta) return false;
    for (size_t i=old_size;i<n;++i) if (p[offset+i]!=0xcc) return false;
    memcpy(p+offset,kip,n);
    if (n<old_size) memset(p+offset+n,0xcc,old_size-n);
    put32(p+header+4,n); put32(p+meta+12,n); hash(p+meta+16,kip,n);
    return true;
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

bool openpak_system_install(char *err,int errlen) {
    if(!recover_file(BOOT) || !recover_file(STORE) || !recover_file(OPENPAK_PACKAGE)) {
        snprintf(err,errlen,"Could not recover interrupted system setup");return false;
    }
    bool ok=false;
    uint8_t *base=NULL,*kip=NULL,*source=NULL,*ca=NULL,*store=NULL,*boot=NULL,*new_boot=NULL;
    size_t n=0,kn=0,sn=0,cn=0,outn=0,bn=0,new_bn=0;
    const char *failure="Could not prepare system support";
    base=read_file(PACKAGE,&n,false);
    kip=read_file(ASSET "system/ams_mitm-1.11.2.kip",&kn,true);
    if(!base||!kip)goto done;
    failure="System support requires unmodified Atmosphere 1.11.2";
    if(!openpak_package_build(base,n,kip,kn))goto done;
    failure="Boot configuration changed; existing file preserved";
    if(!managed_or_original(BOOT,STATE "/boot.managed",STATE "/boot.original",STATE "/boot.absent"))goto done;
    boot=read_file(exists(STATE "/boot.original")?STATE "/boot.original":BOOT,&bn,false);
    failure="No standard Hekate package3 entry found";
    if(!boot || !openpak_boot_build(boot,bn,&new_boot,&new_bn))goto done;
    failure="Certificate overlay changed; existing file preserved";
    if(!managed_or_original(STORE,STATE "/certificate.managed",STATE "/certificate.original",STATE "/certificate.absent"))goto done;
    failure="OpenPak boot package changed; existing file preserved";
    if(exists(OPENPAK_PACKAGE) && !same_file(OPENPAK_PACKAGE,base,n))goto done;
    failure="Could not read the system certificates";
    source=system_store(&sn);ca=read_file(ASSET "ca.der",&cn,true);
    if(!source||!ca)goto done;
    if(openpak_store_is_ours(source,sn,ca,cn)) { store=source;outn=sn;source=NULL; }
    else if(!openpak_store_replace(source,sn,ca,cn,&store,&outn))goto done;
    failure="Could not clear blank_prodinfo_emummc in exosphere.ini";
    if(!openpak_exosphere_blank(false,err,errlen))goto done;
    failure="Could not back up system setup";
    if(!backup(BOOT,STATE "/boot.original",STATE "/boot.absent") ||
       !backup(STORE,STATE "/certificate.original",STATE "/certificate.absent"))goto done;
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
    free(base);free(kip);free(source);free(ca);free(store);free(boot);free(new_boot);
    if(!ok)snprintf(err,errlen,"%s",failure);
    return ok;
}

static bool managed_or_original(const char *p,const char *managed,const char *original,const char *absent) {
    if(!exists(managed) && !exists(original) && !exists(absent))return true;
    if(!exists(p))return exists(absent);
    size_t n=0;uint8_t *b=read_file(p,&n,false);if(!b)return false;
    bool ok=same_file(managed,b,n)||same_file(original,b,n);free(b);return ok;
}
bool openpak_system_remove(char *err,int errlen) {
    if(!recover_file(BOOT) || !recover_file(STORE)) {
        snprintf(err,errlen,"Could not recover interrupted system setup");return false;
    }
    if(!exists(STATE "/boot.original") && !exists(STATE "/certificate.original") && !exists(STATE "/certificate.absent"))return true;
    if(!managed_or_original(BOOT,STATE "/boot.managed",STATE "/boot.original",STATE "/boot.absent") ||
       !managed_or_original(STORE,STATE "/certificate.managed",STATE "/certificate.original",STATE "/certificate.absent")) {
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

#!/usr/bin/env python3
"""Test system setup.

--package3 <official Atmosphere 1.12.0 package3> enables the real-package build, install, upgrade
and rollback tests; --store-package3 <package3-openpak-store> also compares the build byte for
byte with the package that booted on hardware. Neither ships in this repository: when they are not
given, the copies in the OpenPak workspace's scratch folder are used if present, else those tests
are skipped and say so.
"""
import argparse
import ctypes as c
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

repo=Path(__file__).resolve().parents[1]
scratch=repo.parents[1]/'scratch'
def local(p):
    return p if p.is_file() else None
parser=argparse.ArgumentParser()
parser.add_argument('--package3',type=Path,
                    default=local(scratch/'ams-1.12.0/sd/atmosphere/package3'))
parser.add_argument('--store-package3',type=Path,default=local(scratch/'hbstore/pkg3/package3-openpak-store-dnsfix'))
# Store packages earlier releases wrote, both booted on hardware: 0.3.15-0.3.17 (1.11.2) and
# 0.3.18-0.3.19 (1.12.0 without the dns.mitm fix). An upgrade replaces either.
OLD_STORE={'4e2ac49bb8547c8d17af1ebcc092d60a3c4932a7c31ee2e2bd06113e619530e2':scratch/'hbstore/pkg3/package3-openpak-store',
           'eff03265fe175feb424c0d7e42726b13f5ef8eb93824e91ce1556f69175815ca':scratch/'hbstore/pkg3/package3-openpak-store-1.12.0'}
a=parser.parse_args()
# What the builder makes from the official 1.12.0 package (sha 3cc9d6ca…): ams_mitm alone, as every
# build before store trust wrote it, and ams_mitm + Loader + fusee. The 1.11.2 store package of the
# same build booted on hardware and launched a store title (2026-10-04). This one adds upstream's
# firmware-23 dns.mitm fix with OpenPak's correction (tools/atmosphere-dns-*.patch); booted on 23.0.1 hardware 2026-10-07.
PREVIOUS_SHA='cdb45bb91119991404bb3b08a5a6e3e738abd80b99cd1b19ae78be62ce730703'
STORE_SHA='175f94de55aa4cf1f704cd110c4339617df0a339537033502b9bce9cde2e031f'
with tempfile.TemporaryDirectory(prefix='openpak-system-test-') as directory:
    tmp=Path(directory)
    lib=tmp/'system.so'
    stub=tmp/'root.c';stub.write_text('const char *openpak_root = "";\n')
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-DOPENPAK_HOST_TEST',
                    str(repo/'source/system.c'),str(repo/'source/ksp.c'),str(stub),'-lcrypto','-o',str(lib)],check=True)
    api=c.CDLL(str(lib));api.openpak_store_replace.restype=c.c_bool
    api.openpak_store_replace.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.POINTER(c.c_void_p),c.POINTER(c.c_size_t)]
    api.openpak_package_build.restype=c.c_bool
    api.openpak_package_build.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t]
    api.openpak_boot_build.restype=c.c_bool
    api.openpak_boot_build.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(c.c_void_p),c.POINTER(c.c_size_t)]
    api.openpak_system_install.restype=api.openpak_system_remove.restype=c.c_bool
    api.openpak_system_install.argtypes=api.openpak_system_remove.argtypes=[c.c_char_p,c.c_int]
    libc=c.CDLL(None);libc.free.argtypes=[c.c_void_p]
    source=struct.pack('<II',0x546c7373,2)+struct.pack('<IIII',1,1,4,32)+struct.pack('<IIII',1033,1,4,36)+b'keepold!'
    ca=b'new certificate'
    def transform_with(data,cert):
        out=c.c_void_p();size=c.c_size_t()
        ok=api.openpak_store_replace(data,len(data),cert,len(cert),c.byref(out),c.byref(size))
        result=c.string_at(out,size.value) if ok else None
        if out:libc.free(out)
        return result
    def transform(data):
        return transform_with(data,ca)
    result=transform(source);assert result
    assert result[:24]==source[:24] and result[40:48]==source[40:48]
    assert result[48:]==ca and struct.unpack_from('<II',result,32)==(len(ca),40)
    for invalid in [b'',source[:7],source[:30],b'badmagic'+source[8:],source[:36]+struct.pack('<I',0xffffffff)+source[40:]]:
        assert transform(invalid) is None
    dup=bytearray(source);struct.pack_into('<I',dup,24,1);assert transform(bytes(dup)) is None
    bad=c.create_string_buffer(b'x'*0x800000)
    kip=(repo/'romfs/system/ams_mitm-1.12.0.kip').read_bytes()
    loader=(repo/'romfs/system/loader-1.12.0.kip').read_bytes()
    fusee=(repo/'romfs/system/fusee-1.12.0.bin').read_bytes()
    assert not api.openpak_package_build(bad,0x800000,kip,len(kip),None,0,None,0)
    assert not api.openpak_package_build(bad,0x800000,kip,len(kip),loader,len(loader),fusee,len(fusee))
    api.openpak_store_is_ours.restype=c.c_bool;api.openpak_store_is_ours.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t]
    assert not api.openpak_store_is_ours(source,len(source),ca,len(ca))          # stock: 1033 is the old root
    assert api.openpak_store_is_ours(result,len(result),ca,len(ca))              # our overlay read back
    assert not api.openpak_store_is_ours(result,len(result),b'other cert',10)   # someone else's CA in 1033
    print('PASS: certificate bounds, duplicate IDs, preservation, unsupported package rejection, overlay recognised')
    def boot_transform(data):
        out=c.c_void_p();size=c.c_size_t()
        ok=api.openpak_boot_build(data,len(data),c.byref(out),c.byref(size))
        result=c.string_at(out,size.value) if ok else None
        if out:libc.free(out)
        return result
    boot_original=b'[config]\r\nautoboot=4\r\n[CFW]\r\npkg3=atmosphere/package3\r\nemummcforce=1\r\n'
    boot_expected=boot_original.replace(b'pkg3=atmosphere/package3',b'pkg3=atmosphere/package3-openpak')
    assert boot_transform(boot_original)==boot_expected
    assert boot_transform(b'  fss0 = atmosphere/package3  \n')==b'  fss0 = atmosphere/package3-openpak  \n'
    for unsupported in [b'',b'[CFW]\npayload=custom.bin\n',b'pkg3=atmosphere/package3-openpak',b'pkg3=atmosphere/package3\0',b'#pkg3=atmosphere/package3']:
        assert boot_transform(unsupported) is None
    print('PASS: boot-entry selection preserves formatting and rejects unsupported configurations')
    api.openpak_exosphere_blank.restype=c.c_bool;api.openpak_exosphere_blank.argtypes=[c.c_bool,c.c_char_p,c.c_int]
    exo_root=tmp/'exo';(exo_root/'switch/openpak/system').mkdir(parents=True)
    exo_root_bytes=str(exo_root).encode();c.c_char_p.in_dll(api,'openpak_root').value=exo_root_bytes;err=c.create_string_buffer(256)
    exo=exo_root/'exosphere.ini';prev=exo_root/'switch/openpak/system/exosphere.previous'
    assert api.openpak_exosphere_blank(False,err,256) and not exo.exists()          # no file, enable: nothing to do
    assert api.openpak_exosphere_blank(True,err,256),err.value                       # no file, disable: created
    assert exo.read_bytes()==b'[exosphere]\nblank_prodinfo_emummc=1\n'
    exo.write_bytes(b'[exosphere]\r\ndebugmode=1\r\nblank_prodinfo_sysmmc=1\r\nblank_prodinfo_emummc=1\r\nlog_port=0\r\n')
    assert api.openpak_exosphere_blank(False,err,256),err.value                      # enable clears it in place
    assert exo.read_bytes()==b'[exosphere]\r\ndebugmode=1\r\nblank_prodinfo_sysmmc=1\r\nblank_prodinfo_emummc=0\r\nlog_port=0\r\n'
    assert prev.read_bytes().endswith(b'emummc=1\r\nlog_port=0\r\n')
    before=exo.read_bytes();assert api.openpak_exosphere_blank(False,err,256) and exo.read_bytes()==before   # idempotent
    assert api.openpak_exosphere_blank(True,err,256),err.value                       # disable sets it back
    assert exo.read_bytes()==before.replace(b'emummc=0',b'emummc=1')
    exo.write_bytes(b'[exosphere]\ndebugmode=1\n[other]\nx=1\n')                    # key missing: inserted
    assert api.openpak_exosphere_blank(True,err,256),err.value
    assert exo.read_bytes()==b'[exosphere]\nblank_prodinfo_emummc=1\ndebugmode=1\n[other]\nx=1\n'
    print('PASS: exosphere blank_prodinfo_emummc: enable clears, disable sets, missing key inserted, sysmmc untouched, backup kept')
    # The overlay guard, without needing a real package3: whose store is on the card.
    import os
    os.chdir(repo)   # ASSET is romfs/ca.der, relative to the repository
    api.openpak_store_file_is_ours.restype=c.c_bool;api.openpak_store_file_is_ours.argtypes=[]
    guard=tmp/'guard';overlay=guard/'atmosphere/contents/0100000000000800/romfs/ssl_TrustedCerts.bdf'
    overlay.parent.mkdir(parents=True)
    # Keep the bytes alive: openpak_root holds a pointer into this object, not a copy.
    guard_bytes=str(guard).encode();c.c_char_p.in_dll(api,'openpak_root').value=guard_bytes
    real_ca=(repo/'romfs/ca.der').read_bytes()
    assert api.openpak_store_file_is_ours()                                     # nothing there to preserve
    ours=transform_with(source,real_ca)
    overlay.write_bytes(ours);assert api.openpak_store_file_is_ours()           # what this build writes
    overlay.write_bytes(ours+b'\0\0\0\0');assert api.openpak_store_file_is_ours()  # our CA, another layout
    overlay.write_bytes(source);assert not api.openpak_store_file_is_ours()     # the stock store
    overlay.write_bytes(b'somebody else');assert not api.openpak_store_file_is_ours()
    print('PASS: overlay ownership: ours in any layout or absent yields, a store we did not write is kept')
    # A Loader in /atmosphere/kips is loaded by fusee ahead of OpenPak's: named, never touched.
    api.openpak_loader_override.restype=c.c_bool;api.openpak_loader_override.argtypes=[c.c_char_p,c.c_int]
    kroot=tmp/'kips';kroot_bytes=str(kroot).encode();c.c_char_p.in_dll(api,'openpak_root').value=kroot_bytes
    name=c.create_string_buffer(64);kips=kroot/'atmosphere/kips'
    assert not api.openpak_loader_override(name,64)                             # no kips folder
    kips.mkdir(parents=True);assert not api.openpak_loader_override(name,64)    # an empty one
    (kips/'hoc.txt').write_bytes(loader);(kips/'other.kip').write_bytes(kip)   # wrong name; not a Loader
    (kips/'short.kip1').write_bytes(loader[:-1])                                # fusee rejects the size
    assert not api.openpak_loader_override(name,64)
    (kips/'hoc.kip').write_bytes(loader)
    assert api.openpak_loader_override(name,64) and name.value==b'hoc.kip'
    assert (kips/'hoc.kip').read_bytes()==loader
    (kips/'hoc.kip').rename(kips/'hoc.kip1');assert api.openpak_loader_override(name,64) and name.value==b'hoc.kip1'
    print('PASS: a Loader override in /atmosphere/kips is found the way fusee finds it, and left alone')
    # The Album opens the Album; hbmenu while R is held (override_config.ini, [hbl_config]).
    api.openpak_album_build.restype=c.c_int
    api.openpak_album_build.argtypes=[c.c_char_p,c.c_size_t,c.POINTER(c.c_void_p),c.POINTER(c.c_size_t)]
    api.openpak_album_install.restype=api.openpak_album_remove.restype=c.c_bool
    api.openpak_album_install.argtypes=api.openpak_album_remove.argtypes=[c.c_char_p,c.c_int]
    def album(data):
        out=c.c_void_p();size=c.c_size_t()
        r=api.openpak_album_build(data,len(data) if data is not None else 0,c.byref(out),c.byref(size))
        result=c.string_at(out,size.value) if r==1 else r
        if out:libc.free(out)
        return result
    created=b'[hbl_config]\nprogram_id_0=010000000000100D\noverride_key_0=R\n'
    # Atmosphère 1.11.2's config_templates/override_config.ini, as a user would copy it in.
    template=(b'[hbl_config]\n; Program Specific Config\n; Up to 8 program-specific configurations can be set.\n'
              b'; program_id_0=010000000000100D\n; override_address_space=39_bit\n; override_key_0=!R\n\n'
              b'; override_any_app=true\n; override_any_app_key=R\n; path=atmosphere/hbl.nsp\n\n'
              b'[default_config]\n; override_key=!L\n; cheat_enable_key=!L')
    assert album(None)==created
    assert album(template)==template.replace(b'hbl.nsp\n\n',b'hbl.nsp\noverride_key_0=R\n\n')
    crlf=b'; mine\r\n[default_config]\r\noverride_key=!L\r\n[HBL_Config]\r\npath=/x/hbl.nsp\r\noverride_key_0 = !R ; hbmenu\r\noverride_any_app=false\r\n'
    assert album(crlf)==crlf.replace(b'= !R ;',b'= R ;')                                  # in place, comment and CRLF kept
    assert album(b'[default_config]\r\noverride_key=!L')==b'[default_config]\r\noverride_key=!L\r\n[hbl_config]\r\noverride_key_0=R\r\n'
    assert album(b'')==b'[hbl_config]\noverride_key_0=R\n'
    assert album(b'[hbl_config]\nprogram_id=0x010000000000100d\noverride_key: !r\n')==b'[hbl_config]\nprogram_id=0x010000000000100d\noverride_key: R\n'
    assert album(b'[hbl_config]\noverride_key_0=!L\noverride_key_0=!R\n')==b'[hbl_config]\noverride_key_0=!L\noverride_key_0=R\n'   # the last one counts
    assert album(b'[hbl_config]\noverride_key_0=!R\n  R\n')==0                         # a continuation is the value
    assert album(created)==0 and album(b'[hbl_config]\noverride_key_0=r\n')==0
    for foreign in [b'[hbl_config]\nprogram_id_0=0100000000001000\n',               # another title is hbl's
                    b'[hbl_config]\nprogram_id_0=0100000000001000\noverride_key_0=R\n',
                    b'[hbl_config]\noverride_key_0=!L\n',b'[hbl_config]\noverride_key=ZR\n',b'[hbl_config]\noverride_key_0=\n',
                    b'[hbl_config]\noverride_key_0=!R;x\n',                         # no space: part of the value
                    b'[hbl_config]\nprogram_id_3=010000000000100D\n',                # the Album from another slot
                    b'[hbl_config]\n\n  [default_config]\n',                         # would continue our line
                    b'[hbl_config]\nkey\0\n']:
        assert album(foreign)==-1,foreign
    # An indented line after a value continues it: still [hbl_config], so ours goes after it.
    assert album(b'[hbl_config]\nx=1\n\n  [default_config]\n')==b'[hbl_config]\nx=1\n\n  [default_config]\noverride_key_0=R\n'
    assert album(b'[hbl_config]\n; program_id_0=0100000000001000\n[other]\nprogram_id_0=0\n')==\
        b'[hbl_config]\n; program_id_0=0100000000001000\noverride_key_0=R\n[other]\nprogram_id_0=0\n'
    aroot=tmp/'album';astate=aroot/'switch/openpak/system';astate.mkdir(parents=True)
    aroot_bytes=str(aroot).encode();c.c_char_p.in_dll(api,'openpak_root').value=aroot_bytes
    ini=aroot/'atmosphere/config/override_config.ini';previous=Path(str(ini)+'.openpak-previous')
    note=c.create_string_buffer(160)
    def records():
        return sorted(p.name for p in astate.glob('album.*'))
    # No file: created, and disable deletes it.
    assert api.openpak_album_install(note,160),note.value
    assert ini.read_bytes()==created and records()==['album.absent','album.managed'] and note.value==b''
    st=ini.stat();assert api.openpak_album_install(note,160)                          # re-apply: not rewritten
    assert ini.stat().st_ino==st.st_ino and ini.stat().st_mtime_ns==st.st_mtime_ns
    assert api.openpak_album_remove(note,160),note.value
    assert not ini.exists() and records()==[] and ini.parent.is_dir()
    # The record lost while on: a file exactly as OpenPak creates it is still OpenPak's.
    assert api.openpak_album_install(note,160);[p.unlink() for p in astate.glob('album.*')]
    assert api.openpak_album_install(note,160) and records()==['album.absent','album.managed']
    assert api.openpak_album_remove(note,160) and not ini.exists()
    # The user's file, other sections and comments kept byte for byte; disable puts it back.
    for original in [template,crlf]:
        ini.write_bytes(original)
        assert api.openpak_album_install(note,160),note.value
        assert ini.read_bytes()==album(original) and (astate/'album.original').read_bytes()==original
        before=ini.read_bytes();assert api.openpak_album_install(note,160) and ini.read_bytes()==before
        # Power lost mid-write: the file is the .openpak-previous copy; both directions recover.
        ini.rename(previous)
        assert api.openpak_album_install(note,160),note.value
        assert ini.read_bytes()==before and not previous.exists()
        ini.rename(previous)
        assert api.openpak_album_remove(note,160),note.value
        assert ini.read_bytes()==original and not previous.exists() and records()==[]
    # Already R and the user's: nothing written, nothing recorded, nothing to undo.
    ini.write_bytes(b'[hbl_config]\r\noverride_key_0=R\r\n');st=ini.stat()
    assert api.openpak_album_install(note,160) and records()==[]
    assert api.openpak_album_remove(note,160) and ini.stat().st_mtime_ns==st.st_mtime_ns
    # Somebody else's hbl setup: kept, said so, Enable not failed by it.
    for foreign in [b'[hbl_config]\nprogram_id_0=0100000000001000\n',b'[hbl_config]\noverride_key_0=!ZL\n']:
        ini.write_bytes(foreign)
        assert not api.openpak_album_install(note,160)
        assert note.value==b'Album left as it is: override_config.ini has its own Homebrew Menu setup.',note.value
        assert ini.read_bytes()==foreign and records()==[]
        assert api.openpak_album_remove(note,160) and ini.read_bytes()==foreign
    # Changed after OpenPak edited it: whoever changed it keeps it, and the original stays saved.
    ini.write_bytes(template);assert api.openpak_album_install(note,160)
    ini.write_bytes(album(template)+b'\n[default_config]\noverride_key=!R\n')         # still Album/R: fine
    assert api.openpak_album_install(note,160) and note.value==b''
    ini.write_bytes(template)                                                          # the user put !R back
    ini.write_bytes(template.replace(b'; override_key_0=!R',b'override_key_0=!L'))
    assert not api.openpak_album_install(note,160)
    assert note.value==b'Album left as it is: override_config.ini changed after OpenPak edited it.'
    assert not api.openpak_album_remove(note,160) and b'left as it is' in note.value
    assert (astate/'album.original').read_bytes()==template
    ini.write_bytes(album(template))                                                   # back to what we wrote
    assert api.openpak_album_remove(note,160) and ini.read_bytes()==template and records()==[]
    print('PASS: Album: absent/default/other sections kept, own file re-applied, foreign hbl setup kept and noted, '
          'disable restores bytes or deletes, interrupted writes recovered')
    if not a.package3:
        print('SKIP: real-package build, install and upgrade (no --package3, none in the workspace)')
    if a.package3:
        original=a.package3.resolve().read_bytes();os.chdir(repo)
        def build(with_store,l=loader,f=fusee):
            b=c.create_string_buffer(original,len(original))
            ok=api.openpak_package_build(b,len(original),kip,len(kip),l if with_store else None,len(l) if with_store else 0,
                                         f if with_store else None,len(f) if with_store else 0)
            return b.raw[:len(original)] if ok else None
        previous,store=build(False),build(True)
        assert hashlib.sha256(previous).hexdigest()==PREVIOUS_SHA
        assert hashlib.sha256(store).hexdigest()==STORE_SHA
        if a.store_package3:
            assert store==a.store_package3.read_bytes(),'store package differs from the one that booted'
        else:
            print('SKIP: byte comparison with package3-openpak-store (no --store-package3, none in the workspace)')
        # Strict inputs: the wrong component in either slot, or only half of store trust, builds nothing.
        renamed=bytearray(loader);renamed[4:10]=b'Lodder'
        other_id=bytearray(loader);other_id[0x10]=2
        grown=bytes(loader)+b'\0'                                                # size no longer the header's
        for l,f in [(bytes(renamed),fusee),(bytes(other_id),fusee),(grown,fusee),(kip,fusee),
                    (loader,fusee+b'\0'*(0x20001-len(fusee))),(loader,b'\0'*len(fusee)),(loader,fusee[:0x80])]:
            assert build(True,l,f) is None
        b=c.create_string_buffer(original,len(original))
        assert not api.openpak_package_build(b,len(original),kip,len(kip),loader,len(loader),None,0)
        assert not api.openpak_package_build(b,len(original),kip,len(kip),None,0,fusee,len(fusee))
        assert b.raw[:len(original)]==original
        print('PASS: store package = %s…, previous build = %s…, bad Loader/fusee refused, base untouched'
              %(STORE_SHA[:8],PREVIOUS_SHA[:8]))
        for existing in [None,b'existing user overlay']:
            root=tmp/('absent' if existing is None else 'existing');state=root/'switch/openpak/system'
            state.mkdir(parents=True);(state/'source.bdf').write_bytes(source)
            package=root/'atmosphere/package3';package.parent.mkdir();package.write_bytes(original)
            original_stat=package.stat()
            alternate=root/'atmosphere/package3-openpak'
            boot=root/'bootloader/hekate_ipl.ini';boot.parent.mkdir();boot.write_bytes(boot_original)
            overlay=root/'atmosphere/contents/0100000000000800/romfs/ssl_TrustedCerts.bdf'
            if existing is not None:overlay.parent.mkdir(parents=True);overlay.write_bytes(existing)
            root_bytes=str(root).encode();c.c_char_p.in_dll(api,'openpak_root').value=root_bytes
            err=c.create_string_buffer(256)
            assert api.openpak_system_install(err,256),err.value
            assert boot.read_bytes()==boot_expected and package.read_bytes()==original
            patched=alternate.read_bytes();assert patched==store
            alternate_stat=alternate.stat()
            assert api.openpak_system_install(err,256),err.value
            assert alternate.stat().st_mtime_ns==alternate_stat.st_mtime_ns
            assert alternate.stat().st_ino==alternate_stat.st_ino
            # Recovery after a power loss between the two configuration renames.
            boot.rename(str(boot)+'.openpak-previous')
            assert api.openpak_system_install(err,256),err.value
            # Preserve unrelated certificate or boot configuration edits.
            overlay.write_bytes(b'changed elsewhere')
            assert not api.openpak_system_install(err,256)
            assert overlay.read_bytes()==b'changed elsewhere'
            assert not api.openpak_system_remove(err,256)
            assert boot.read_bytes()==boot_expected
            # An overlay of ours in another layout (what an older build wrote): re-apply replaces it.
            overlay.write_bytes((state/'certificate.managed').read_bytes()+b'\0\0\0\0')
            assert api.openpak_system_install(err,256),err.value
            assert overlay.read_bytes()==(state/'certificate.managed').read_bytes()
            # An overlay that is not there at all: nothing to preserve, so it is written again.
            overlay.unlink()
            assert api.openpak_system_install(err,256),err.value
            assert overlay.read_bytes()==(state/'certificate.managed').read_bytes()
            # The backup metadata deleted under an active overlay: ours is not the original, so
            # disable must take it away rather than restore it. The metadata is put back
            # afterwards, since the rest of this round still checks the original is honoured.
            names=['certificate.original','certificate.absent','certificate.managed']
            kept={n:(state/n).read_bytes() for n in names if (state/n).exists()}
            for n in names:(state/n).unlink(missing_ok=True)
            assert api.openpak_system_install(err,256),err.value
            assert (state/'certificate.absent').exists() and not (state/'certificate.original').exists()
            for n in names:(state/n).unlink(missing_ok=True)
            for n,data in kept.items():(state/n).write_bytes(data)
            overlay.write_bytes((state/'certificate.managed').read_bytes())
            boot.write_bytes(boot_expected+b'# user edit\n')
            assert not api.openpak_system_remove(err,256)
            boot.write_bytes(boot_expected)
            assert api.openpak_system_remove(err,256),err.value
            assert boot.read_bytes()==boot_original
            assert package.read_bytes()==original
            assert package.stat().st_mtime_ns==original_stat.st_mtime_ns
            assert package.stat().st_ino==original_stat.st_ino
            assert (overlay.read_bytes() if overlay.exists() else None)==existing
            assert api.openpak_system_remove(err,256),err.value
            assert api.openpak_system_install(err,256),err.value
            assert alternate.stat().st_ino==alternate_stat.st_ino
            assert api.openpak_system_remove(err,256),err.value
        print('PASS: enable, reapply, interrupted activation, our own overlay replaced, conflict refusal, disable; active package untouched')
        # Upgrade: every console enabled before store trust has the ams_mitm-only package at
        # package3-openpak. That one file is replaced; any other is still preserved.
        root=tmp/'upgrade';state=root/'switch/openpak/system';state.mkdir(parents=True)
        (state/'source.bdf').write_bytes(source)
        package=root/'atmosphere/package3';package.parent.mkdir();package.write_bytes(original)
        alternate=root/'atmosphere/package3-openpak'
        boot=root/'bootloader/hekate_ipl.ini';boot.parent.mkdir();boot.write_bytes(boot_original)
        root_bytes=str(root).encode();c.c_char_p.in_dll(api,'openpak_root').value=root_bytes
        err=c.create_string_buffer(256)
        alternate.write_bytes(previous)                                   # disabled, older build's file
        assert api.openpak_system_install(err,256),err.value
        assert alternate.read_bytes()==store and boot.read_bytes()==boot_expected
        alternate.write_bytes(previous)                                   # enabled by an older build
        assert api.openpak_system_install(err,256),err.value
        assert alternate.read_bytes()==store
        stat=alternate.stat()
        assert api.openpak_system_install(err,256),err.value             # re-apply: not rewritten
        assert alternate.stat().st_ino==stat.st_ino and alternate.stat().st_mtime_ns==stat.st_mtime_ns
        # Power lost mid-upgrade: the older package is still the .openpak-previous copy.
        alternate.rename(str(alternate)+'.openpak-previous');Path(str(alternate)+'.openpak-previous').write_bytes(previous)
        assert api.openpak_system_install(err,256),err.value
        assert alternate.read_bytes()==store and not Path(str(alternate)+'.openpak-previous').exists()
        # Anything else at package3-openpak is somebody's: kept, and setup stops.
        for foreign in [previous[:0x200000]+b'\x00'+previous[0x200001:],store[:-1]+b'\x01',b'not a package']:
            alternate.write_bytes(foreign)
            assert not api.openpak_system_install(err,256)
            assert err.value==b'OpenPak boot package changed; existing file preserved',err.value
            assert alternate.read_bytes()==foreign and boot.read_bytes()==boot_expected
        # The packages earlier releases wrote are OpenPak's too: replaced, not preserved.
        for want,path in OLD_STORE.items():
            if not path.is_file():
                print('SKIP: upgrade from store package %s… (none in the workspace)'%want[:8]);continue
            old=path.read_bytes();assert hashlib.sha256(old).hexdigest()==want
            alternate.write_bytes(old)
            assert api.openpak_system_install(err,256),err.value
            assert alternate.read_bytes()==store
        alternate.write_bytes(previous)
        assert api.openpak_system_install(err,256),err.value
        # Disable after the upgrade: the boot entry goes back, package3 untouched, the inactive
        # store package kept and reused as it is on the next enable.
        assert api.openpak_system_remove(err,256),err.value
        assert boot.read_bytes()==boot_original and package.read_bytes()==original and alternate.read_bytes()==store
        stat=alternate.stat()
        assert api.openpak_system_install(err,256),err.value
        assert alternate.stat().st_ino==stat.st_ino and boot.read_bytes()==boot_expected
        assert api.openpak_system_remove(err,256),err.value
        print('PASS: upgrade from the ams_mitm-only package (enabled, disabled, interrupted), foreign package kept, reapply, disable')
    # Save Data Cloud (docs/save-data-cloud.md): enable writes patches.ini's two [FS:…] sections and
    # asks for them from the entries that boot this MMC; Nintendo takes both away; others' bytes stay.
    if a.package3:
        fw=(c.c_char*16).in_dll(api,'openpak_test_firmware');emu=c.c_bool.in_dll(api,'openpak_test_emummc')
        api.openpak_ksp_note.restype=c.c_char_p
        root=tmp/'ksp';state=root/'switch/openpak/system';state.mkdir(parents=True)
        (state/'source.bdf').write_bytes(source)
        package=root/'atmosphere/package3';package.parent.mkdir();package.write_bytes(original)
        boot=root/'bootloader/hekate_ipl.ini';boot.parent.mkdir()
        ipl=(b'[config]\nautoboot=1\n\n[CFW (emuMMC)]\npkg3=atmosphere/package3\nkip1patch=nogc\nemummcforce=1\n\n'
             b'[CFW (sysMMC)]\npkg3=atmosphere/package3\nemummc_force_disable=1\n\n[Stock]\nfss0=atmosphere/package3\nstock=1\nemummc_force_disable=1\n')
        boot.write_bytes(ipl)
        (root/'emuMMC').mkdir();(root/'emuMMC/emummc.ini').write_bytes(b'[emummc]\nenabled=1\nsector=0x70db8000\n')
        patches=root/'bootloader/patches.ini';user=b'# sigpatches\n[FS:34383ee799926340]\n.nosigchk=0:0x194A0:0x4:BA090094,E0031F2A\n'
        patches.write_bytes(user)
        log=state/'save-data-cloud.log';dry=root/'switch/openpak/save-data-cloud.dry-run'
        root_bytes=str(root).encode();c.c_char_p.in_dll(api,'openpak_root').value=root_bytes
        err=c.create_string_buffer(256)
        openpak=boot_transform(ipl)
        def asks(entry):
            return b'[%s]\npkg3=atmosphere/package3-openpak\nkip1patch=openpak_ksp\n'%entry in boot.read_bytes()
        # Another firmware: OpenPak goes on, Save Data Cloud does not, and says why.
        fw.value=b'22.5.0'
        assert api.openpak_system_install(err,256),err.value
        assert b'needs firmware 23.0.0 or 23.0.1' in api.openpak_ksp_note() and patches.read_bytes()==user
        assert boot.read_bytes()==openpak and b'kip1patch=openpak_ksp' not in boot.read_bytes()
        assert api.openpak_system_remove(err,256) and boot.read_bytes()==ipl
        # 23.0.1 on emuMMC: their sigpatches kept, ours after them; only the emuMMC entry asks.
        fw.value=b'23.0.1';emu.value=True
        assert api.openpak_system_install(err,256),err.value
        assert api.openpak_ksp_note()==b'',api.openpak_ksp_note()
        p=patches.read_bytes()
        assert p.startswith(user) and p.count(b'.openpak_ksp=1:0x')==8 and b'[FS:fdaf163288e10805]\n' in p
        assert asks(b'CFW (emuMMC)') and not asks(b'CFW (sysMMC)') and boot.read_bytes().count(b'openpak_ksp')==1
        assert (state/'boot.managed').read_bytes()==boot.read_bytes() and (state/'patches.ini.previous').read_bytes()==user
        assert b'+ kip1patch=openpak_ksp' in log.read_bytes() and b'+ [FS:34383ee799926340]' in log.read_bytes()
        st=patches.stat()
        assert api.openpak_system_install(err,256),err.value                   # re-apply: nothing rewritten
        assert patches.stat().st_mtime_ns==st.st_mtime_ns and asks(b'CFW (emuMMC)')
        # Nintendo: the request goes with boot.original, then our sections; their file byte for byte.
        assert api.openpak_system_remove(err,256),err.value
        assert boot.read_bytes()==ipl and patches.read_bytes()==user and api.openpak_ksp_note()==b''
        # The same on sysMMC: only the sysMMC CFW entry, never the stock one.
        emu.value=False
        assert api.openpak_system_install(err,256),err.value
        assert asks(b'CFW (sysMMC)') and not asks(b'CFW (emuMMC)') and boot.read_bytes().count(b'openpak_ksp')==1
        assert api.openpak_system_remove(err,256) and boot.read_bytes()==ipl and patches.read_bytes()==user
        # No patches.ini: created, and deleted again by Nintendo.
        emu.value=True;patches.unlink()
        assert api.openpak_system_install(err,256) and patches.read_bytes().startswith(b'# OpenPak Save Data Cloud')
        assert (state/'patches.absent').exists()
        assert api.openpak_system_remove(err,256) and not patches.exists() and not (state/'patches.absent').exists()
        # Somebody edits patches.ini while it is on: their edit stays when ours goes.
        assert api.openpak_system_install(err,256)
        patches.write_bytes(b'[Loader:0123456789abcdef]\n.x=0:0x0:0x1:00,01\n'+patches.read_bytes())
        assert api.openpak_system_remove(err,256) and patches.read_bytes()==b'[Loader:0123456789abcdef]\n.x=0:0x0:0x1:00,01\n'
        # hekate_ipl.ini changed by somebody: the remove is refused, and patches.ini, still asked for, stays.
        patches.write_bytes(user)
        assert api.openpak_system_install(err,256)
        boot.write_bytes(boot.read_bytes()+b'# user edit\n');before=patches.read_bytes()
        assert not api.openpak_system_remove(err,256) and patches.read_bytes()==before
        boot.write_bytes((state/'boot.managed').read_bytes())
        assert api.openpak_system_remove(err,256) and patches.read_bytes()==user
        # A patches.ini OpenPak cannot edit: left alone, no request, OpenPak still goes on.
        patches.write_bytes(b'[FS:34383ee799926340]\n\0\n')
        assert api.openpak_system_install(err,256),err.value
        assert b'not one OpenPak can edit' in api.openpak_ksp_note() and patches.read_bytes()==b'[FS:34383ee799926340]\n\0\n'
        assert b'openpak_ksp' not in boot.read_bytes()
        assert api.openpak_system_remove(err,256);patches.write_bytes(user)
        # No hekate entry boots this MMC: nothing written.
        (root/'emuMMC/emummc.ini').write_bytes(b'[emummc]\nenabled=0\n')
        assert api.openpak_system_install(err,256) and b'no hekate entry boots this emuMMC' in api.openpak_ksp_note()
        assert patches.read_bytes()==user and b'openpak_ksp' not in boot.read_bytes()
        assert api.openpak_system_remove(err,256)
        (root/'emuMMC/emummc.ini').write_bytes(b'[emummc]\nenabled=1\n')
        # Dry run: the log says what would change, nothing does.
        dry.write_bytes(b'')
        assert api.openpak_system_install(err,256) and b'dry run' in api.openpak_ksp_note()
        assert patches.read_bytes()==user and b'openpak_ksp' not in boot.read_bytes()
        text=log.read_bytes()
        assert b'(dry run, nothing written)' in text and b'+ kip1patch=openpak_ksp' in text and text.count(b'+ .openpak_ksp=')==8
        assert api.openpak_system_remove(err,256);dry.unlink()
        fw.value=b''
        print('PASS: Save Data Cloud: 23.0.x only, entries of this MMC only (never stock), sigpatches kept, re-apply '
              'idempotent, Nintendo removes both, absent file deleted again, foreign/unreadable files kept, dry run')

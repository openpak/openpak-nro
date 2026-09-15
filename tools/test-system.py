#!/usr/bin/env python3
"""Test system setup. Optional --package3 enables a real-package install/rollback test."""
import argparse
import ctypes as c
from pathlib import Path
import struct
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--package3',type=Path)
a=parser.parse_args()
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='openpak-system-test-') as directory:
    tmp=Path(directory)
    lib=tmp/'system.so'
    stub=tmp/'root.c';stub.write_text('const char *openpak_root = "";\n')
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-DOPENPAK_HOST_TEST',
                    str(repo/'source/system.c'),str(stub),'-lcrypto','-o',str(lib)],check=True)
    api=c.CDLL(str(lib));api.openpak_store_replace.restype=c.c_bool
    api.openpak_store_replace.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.POINTER(c.c_void_p),c.POINTER(c.c_size_t)]
    api.openpak_package_build.restype=c.c_bool
    api.openpak_package_build.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t]
    api.openpak_boot_build.restype=c.c_bool
    api.openpak_boot_build.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(c.c_void_p),c.POINTER(c.c_size_t)]
    api.openpak_system_install.restype=api.openpak_system_remove.restype=c.c_bool
    api.openpak_system_install.argtypes=api.openpak_system_remove.argtypes=[c.c_char_p,c.c_int]
    libc=c.CDLL(None);libc.free.argtypes=[c.c_void_p]
    source=struct.pack('<II',0x546c7373,2)+struct.pack('<IIII',1,1,4,32)+struct.pack('<IIII',1033,1,4,36)+b'keepold!'
    ca=b'new certificate'
    def transform(data):
        out=c.c_void_p();size=c.c_size_t()
        ok=api.openpak_store_replace(data,len(data),ca,len(ca),c.byref(out),c.byref(size))
        result=c.string_at(out,size.value) if ok else None
        if out:libc.free(out)
        return result
    result=transform(source);assert result
    assert result[:24]==source[:24] and result[40:48]==source[40:48]
    assert result[48:]==ca and struct.unpack_from('<II',result,32)==(len(ca),40)
    for invalid in [b'',source[:7],source[:30],b'badmagic'+source[8:],source[:36]+struct.pack('<I',0xffffffff)+source[40:]]:
        assert transform(invalid) is None
    dup=bytearray(source);struct.pack_into('<I',dup,24,1);assert transform(bytes(dup)) is None
    bad=c.create_string_buffer(b'x'*0x800000)
    kip=(repo/'romfs/system/ams_mitm-1.11.2.kip').read_bytes()
    assert not api.openpak_package_build(bad,0x800000,kip,len(kip))
    print('PASS: certificate bounds, duplicate IDs, preservation, unsupported package rejection')
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
    if a.package3:
        import os
        original=a.package3.resolve().read_bytes();os.chdir(repo)
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
            patched=alternate.read_bytes();assert patched!=original and len(patched)==len(original)
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
        print('PASS: enable, reapply, interrupted activation, conflict refusal, disable; active package untouched')

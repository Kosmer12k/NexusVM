#!/usr/bin/env python3
"""
NexusVM GSI Tool (gsi_tool.py)
Automated GSI Downloader, Unpacker, Patcher, and RootFS Builder for Android 12 ARM64.

Usage:
  python3 gsi_tool.py download --version 12 --type vanilla
  python3 gsi_tool.py unpack --image system.img --output ./rootfs
  python3 gsi_tool.py patch --rootfs ./rootfs --model "ROG Phone 6"
  python3 gsi_tool.py package --rootfs ./rootfs --output rootfs_a12_arm64.tar.gz
  python3 gsi_tool.py all (Runs complete pipeline)
"""

import os
import sys
import argparse
import subprocess
import shutil
import urllib.request
import re

# Popular community A12 GSIs (ARM64, A/B, Vanilla/Slim for optimal container performance)
GSI_RELEASES = {
    "12": {
        "name": "LineageOS 19.1 GSI (Android 12.1 / ARM64 Vanilla)",
        "url": "https://github.com/phhusson/treble_experimentations/releases/download/v416/lineage-19.1-20220813-UNOFFICIAL-arm64_bvN.img.xz",
        "filename": "lineage-19.1-arm64_bvN.img.xz",
        "raw_img": "lineage-19.1-arm64_bvN.img"
    },
    "12-aosp": {
        "name": "AOSP 12.1 v416 GSI (ARM64 Vanilla)",
        "url": "https://github.com/phhusson/treble_experimentations/releases/download/v416/system-squeak-arm64-bvN.img.xz",
        "filename": "system-squeak-arm64-bvN.img.xz",
        "raw_img": "system-squeak-arm64-bvN.img"
    }
}

class GSITool:
    def __init__(self, work_dir="./workspace"):
        self.work_dir = os.path.abspath(work_dir)
        os.makedirs(self.work_dir, exist_ok=True)

    def download_gsi(self, version_key="12"):
        if version_key not in GSI_RELEASES:
            print(f"[!] Invalid version key: {version_key}. Available: {list(GSI_RELEASES.keys())}")
            return None

        info = GSI_RELEASES[version_key]
        dest_path = os.path.join(self.work_dir, info["filename"])
        raw_path = os.path.join(self.work_dir, info["raw_img"])

        if os.path.exists(raw_path):
            print(f"[*] Raw image already exists: {raw_path}")
            return raw_path

        if not os.path.exists(dest_path):
            print(f"[*] Downloading {info['name']}...")
            print(f"    URL: {info['url']}")
            
            def report_progress(block_num, block_size, total_size):
                downloaded = block_num * block_size
                if total_size > 0:
                    percent = min(100, (downloaded / total_size) * 100)
                    mb_down = downloaded / (1024 * 1024)
                    mb_total = total_size / (1024 * 1024)
                    sys.stdout.write(f"\r    Downloading: {percent:.1f}% ({mb_down:.1f}/{mb_total:.1f} MB)")
                    sys.stdout.flush()

            try:
                urllib.request.urlretrieve(info["url"], dest_path, reporthook=report_progress)
                print("\n[+] Download completed successfully.")
            except Exception as e:
                print(f"\n[!] Download failed: {e}")
                print(f"[*] You can manually place an ARM64 A12 GSI system.img into: {self.work_dir}")
                return None

        # Decompress .xz
        print(f"[*] Decompressing {dest_path}...")
        if dest_path.endswith(".xz"):
            ret = subprocess.call(["xz", "-d", "-k", "-f", dest_path])
            if ret != 0:
                # Try 7z fallback
                subprocess.call(["7z", "x", dest_path, f"-o{self.work_dir}", "-y"])
        
        if os.path.exists(raw_path):
            print(f"[+] Raw GSI ready at: {raw_path}")
            return raw_path
        else:
            # Check if any .img was extracted
            for f in os.listdir(self.work_dir):
                if f.endswith(".img"):
                    found = os.path.join(self.work_dir, f)
                    print(f"[+] Extracted image: {found}")
                    return found
            return None

    def unpack_gsi(self, image_path, output_dir):
        output_dir = os.path.abspath(output_dir)
        os.makedirs(output_dir, exist_ok=True)
        print(f"[*] Unpacking {image_path} to {output_dir}...")

        # 1. Check if sparse image and convert using 7z or simg2img
        print("    [1/3] Extracting image structure...")
        
        # Method A: Use 7z (handles ext4/erofs image extraction in userspace without root!)
        cmd_7z = ["7z", "x", image_path, f"-o{output_dir}", "-y"]
        ret = subprocess.call(cmd_7z, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        # Verify if 7z extracted /system contents
        system_dir = os.path.join(output_dir, "system")
        if not os.path.exists(system_dir) and os.path.exists(os.path.join(output_dir, "bin")):
            # It extracted flat system contents directly into output_dir
            # Reorganize so output_dir has standard /system
            temp_sys = os.path.join(self.work_dir, "temp_sys")
            os.rename(output_dir, temp_sys)
            os.makedirs(output_dir, exist_ok=True)
            os.rename(temp_sys, system_dir)

        # Method B: debugfs fallback if 7z didn't yield full directory
        if not os.path.exists(os.path.join(output_dir, "system", "bin")) and not os.path.exists(os.path.join(output_dir, "bin")):
            print("    [!] 7z extraction incomplete, attempting debugfs extraction...")
            self._extract_with_debugfs(image_path, output_dir)

        print(f"[+] RootFS extracted to {output_dir}")
        return output_dir

    def _extract_with_debugfs(self, img_path, target_dir):
        # Uses debugfs rdump to dump files without mount
        dump_cmd = f"debugfs -R 'rdump / {target_dir}' {img_path}"
        subprocess.call(dump_cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def patch_rootfs(self, rootfs_dir, model_name="ROG Phone 6", brand_name="ASUS"):
        rootfs_dir = os.path.abspath(rootfs_dir)
        print(f"[*] Patching RootFS for container execution ({rootfs_dir})...")

        system_dir = os.path.join(rootfs_dir, "system")
        if not os.path.exists(system_dir):
            system_dir = rootfs_dir

        # 1. Create essential skeleton directories
        dirs_to_create = [
            "data", "data/data", "data/app", "data/user/0", "data/system", "data/misc",
            "proc", "sys", "dev", "dev/input", "dev/socket", "dev/pts",
            "cache", "mnt", "storage", "storage/emulated/0",
            "apex", "linkerconfig", "tmp", "vendor", "vendor/lib64"
        ]
        for d in dirs_to_create:
            os.makedirs(os.path.join(rootfs_dir, d), exist_ok=True)

        # 2. Patch build.prop
        build_prop = os.path.join(system_dir, "build.prop")
        print(f"    [1/4] Patching build.prop: Model -> {model_name}, Brand -> {brand_name}")
        
        props_to_override = {
            "ro.product.model": model_name,
            "ro.product.brand": brand_name.lower(),
            "ro.product.manufacturer": brand_name,
            "ro.product.name": model_name.replace(" ", "_").lower(),
            "ro.product.device": model_name.replace(" ", "_").lower(),
            "ro.secure": "0",
            "ro.debuggable": "1",
            "ro.adb.secure": "0",
            "ro.build.type": "userdebug",
            "ro.boot.selinux": "permissive",
            "selinux.reload_policy": "0",
            "persist.sys.dalvik.vm.lib.2": "libart.so",
            "dalvik.vm.heapsize": "512m",
            "dalvik.vm.heapgrowthlimit": "256m",
            "ro.nexusvm.container": "1"
        }

        if os.path.exists(build_prop):
            with open(build_prop, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            
            new_lines = []
            written_keys = set()
            for line in lines:
                stripped = line.strip()
                if "=" in stripped and not stripped.startswith("#"):
                    k = stripped.split("=")[0].strip()
                    if k in props_to_override:
                        new_lines.append(f"{k}={props_to_override[k]}\n")
                        written_keys.add(k)
                        continue
                new_lines.append(line)
            
            for k, v in props_to_override.items():
                if k not in written_keys:
                    new_lines.append(f"{k}={v}\n")
            
            with open(build_prop, "w", encoding="utf-8") as f:
                f.writelines(new_lines)
        else:
            with open(build_prop, "w", encoding="utf-8") as f:
                for k, v in props_to_override.items():
                    f.write(f"{k}={v}\n")

        # 3. Disable crashing hardware HAL services in init.rc
        print("    [2/4] Disabling crashing hardware HAL services...")
        init_files = []
        for r, _, files in os.walk(os.path.join(system_dir, "etc", "init")):
            for f in files:
                if f.endswith(".rc"):
                    init_files.append(os.path.join(r, f))
        
        crash_services = [
            "vold", "wificond", "hostapd", "netd", "gatekeeperd", "fingerprintd",
            "cameraserver", "audioserver", "thermalservice", "healthd", "lmkd"
        ]
        
        for init_file in init_files:
            try:
                with open(init_file, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                modified = False
                for svc in crash_services:
                    pattern = rf"(\n\s*service\s+{svc}\s+)"
                    if re.search(pattern, content):
                        content = re.sub(pattern, rf"\n# [NexusVM disabled] service {svc} ", content)
                        modified = True
                if modified:
                    with open(init_file, "w", encoding="utf-8") as f:
                        f.write(content)
            except Exception:
                pass

        # 4. Generate default environment configs
        print("    [3/4] Generating NexusVM container environment configs...")
        default_prop = os.path.join(rootfs_dir, "default.prop")
        with open(default_prop, "w", encoding="utf-8") as f:
            f.write("ro.secure=0\nro.debuggable=1\nro.nexusvm.container=1\n")

        print("    [4/4] Patching complete.")
        return True

    def package_rootfs(self, rootfs_dir, output_archive):
        output_archive = os.path.abspath(output_archive)
        rootfs_dir = os.path.abspath(rootfs_dir)
        print(f"[*] Packaging RootFS into archive: {output_archive}...")
        
        # Create tar.gz
        cmd = ["tar", "-czf", output_archive, "-C", rootfs_dir, "."]
        ret = subprocess.call(cmd)
        if ret == 0:
            size_mb = os.path.getsize(output_archive) / (1024 * 1024)
            print(f"[+] RootFS package created successfully: {output_archive} ({size_mb:.1f} MB)")
            return output_archive
        else:
            print(f"[!] Packaging failed with code {ret}")
            return None


def main():
    parser = argparse.ArgumentParser(description="NexusVM Android GSI Tool")
    parser.add_argument("command", choices=["download", "unpack", "patch", "package", "all"],
                        help="Action to perform")
    parser.add_argument("--version", default="12", help="Android version (default: 12)")
    parser.add_argument("--image", default=None, help="Path to system.img")
    parser.add_argument("--rootfs", default="./rootfs", help="Path to rootfs directory")
    parser.add_argument("--output", default="nexusvm_rootfs_a12.tar.gz", help="Output package name")
    parser.add_argument("--model", default="ROG Phone 6", help="Spoofed device model")
    parser.add_argument("--brand", default="ASUS", help="Spoofed device brand")

    args = parser.parse_args()
    tool = GSITool(work_dir="./workspace")

    if args.command == "download":
        tool.download_gsi(args.version)
    elif args.command == "unpack":
        if not args.image:
            print("[!] Please provide --image path/to/system.img")
            sys.exit(1)
        tool.unpack_gsi(args.image, args.rootfs)
    elif args.command == "patch":
        tool.patch_rootfs(args.rootfs, model_name=args.model, brand_name=args.brand)
    elif args.command == "package":
        tool.package_rootfs(args.rootfs, args.output)
    elif args.command == "all":
        print("=== NexusVM Automated Pipeline ===")
        raw_img = tool.download_gsi(args.version)
        if raw_img and os.path.exists(raw_img):
            rootfs_path = tool.unpack_gsi(raw_img, args.rootfs)
            tool.patch_rootfs(rootfs_path, model_name=args.model, brand_name=args.brand)
            tool.package_rootfs(rootfs_path, args.output)
            print("=== Done! NexusVM RootFS is ready for deployment ===")

if __name__ == "__main__":
    main()

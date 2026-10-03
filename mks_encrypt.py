Import("env")  # noqa: F821  (fourni par SCons/PlatformIO)

import os

# Clé et plage identiques à encrypt_mks de Marlin (buildroot/share/PlatformIO/scripts/marlin.py)
KEY = bytes([0xA3, 0xBD, 0xAD, 0x0D, 0x41, 0x11, 0xBB, 0x8D, 0xDC, 0x80, 0x2D, 0xD0, 0xD2, 0xC4, 0x9B, 0x1E,
             0x26, 0xEB, 0xE3, 0x33, 0x4A, 0x15, 0xE4, 0x0A, 0xB3, 0xB1, 0x3C, 0x93, 0xBB, 0xAF, 0xF7, 0x3E])
START, END = 320, 31040   # positions START <= p < END


def xor_mks(data):
    out = bytearray(data)
    for p in range(START, min(END, len(out))):
        out[p] ^= KEY[p & 31]
    return bytes(out)   # XOR : la même fonction chiffre et déchiffre


def encrypt_firmware(source, target, env):
    src, dst = str(source[0]), str(target[0])
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(src, "rb") as f:
        data = f.read()
    with open(dst, "wb") as f:
        f.write(xor_mks(data))
    print("MKS: %s (%d octets) -> %s chiffre" % (src, len(data), dst))


# Vraie cible SCons : out/Robin_nano35.bin dépend de firmware.bin (donc la construit si besoin)
# et est rattachée à "buildprog", la cible par défaut de `pio run`.
mks_bin = env.Command(  # noqa: F821
    os.path.join("$PROJECT_DIR", env.GetProjectOption("custom_mks_out", "out"), "Robin_nano35.bin"),
    "$BUILD_DIR/${PROGNAME}.bin",
    env.Action(encrypt_firmware, None),  # noqa: F821
)
env.Depends("buildprog", mks_bin)  # noqa: F821

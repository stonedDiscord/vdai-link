import datetime
import os
import subprocess

Import("env")

VERSION_FILE = 'version.h'

branch = subprocess.run(['git', 'symbolic-ref', '--short', 'HEAD'], stdout=subprocess.PIPE).stdout.decode(
    'utf-8').strip()
revision = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], stdout=subprocess.PIPE).stdout.decode(
    'utf-8').strip()

VERSION = "{} - {} - {}".format(branch, datetime.datetime.now().isoformat(sep=' ', timespec='seconds'), revision)

VERSION_CONTENTS = """
#pragma once
#define VERSION esp-link {}
""".format(VERSION)

INCLUDE_DIR = ".include"

if os.environ.get('PLATFORMIO_INCLUDE_DIR') is not None:
    VERSION_FILE = os.path.join(os.environ.get('PLATFORMIO_INCLUDE_DIR'), VERSION_FILE)
elif os.path.exists(INCLUDE_DIR):
    VERSION_FILE = os.path.join(INCLUDE_DIR, VERSION_FILE)
else:
    PROJECT_DIR = env.subst("$PROJECT_DIR")
    print(os.path.join(PROJECT_DIR, INCLUDE_DIR))
    os.mkdir(os.path.join(PROJECT_DIR, INCLUDE_DIR))
    VERSION_FILE = os.path.join(INCLUDE_DIR, VERSION_FILE)

print("Updating {} with version/timestamp... {}".format(VERSION_FILE, VERSION))
with open(VERSION_FILE, 'w+') as FILE:
    FILE.write(VERSION_CONTENTS)

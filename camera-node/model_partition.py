# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kevin Ball
# PlatformIO cannot run the ESP-IDF component step that packs the person model and flashes it to its partition,
# so this does it: before an upload it packs the model with ESP-DL's own script and adds it to the esptool call
# at the offset of the "pedestrian_det" partition (partitions.csv).
import csv
import os
import subprocess

Import("env")  # noqa: F821  (SCons injects this)

MODEL = "managed_components/espressif__pedestrian_detect/models/s3/pedestrian_detect_pico_s8_v1.espdl"
PACKER = "managed_components/espressif__esp-dl/fbs_loader/pack_espdl_models.py"
PARTITION = "pedestrian_det"


def partition_offset(project_dir, table):
    with open(os.path.join(project_dir, table), newline="") as f:
        for row in csv.reader(line for line in f if not line.startswith("#")):
            if row and row[0].strip() == PARTITION:
                return row[3].strip()
    raise RuntimeError("no %s partition in %s" % (PARTITION, table))


def add_model(source, target, env):
    project = env.subst("$PROJECT_DIR")
    out = os.path.join(env.subst("$BUILD_DIR"), "espdl_models", "pedestrian_detect.espdl")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subprocess.check_call(
        [env.subst("$PYTHONEXE"), os.path.join(project, PACKER),
         "--model_path", os.path.join(project, MODEL), "--out_file", out])
    offset = partition_offset(project, env.GetProjectOption("board_build.partitions"))
    env.Append(UPLOADERFLAGS=[offset, out])
    print("person model %s -> flash %s" % (os.path.basename(out), offset))


env.AddPreAction("upload", add_model)  # noqa: F821

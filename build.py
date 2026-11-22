import os
import shutil
Import("env")

# Builds the TypeScript web worker and copies it into the web UI folder.
# Uses Python for file operations so it works from Windows cmd as well as
# Unix shells (the original used rm/cp, which cmd doesn't have).

project_dir = env.subst("$PROJECT_DIR")
tsc_dir = os.path.join(project_dir, "tsc")
worker_dir = os.path.join(project_dir, "data", "www", "worker")
bundle = os.path.join(tsc_dir, "dist_packed", "main.js")

print("Transpiling TS code")

if os.path.isdir(worker_dir):
    for name in os.listdir(worker_dir):
        path = os.path.join(worker_dir, name)
        if os.path.isfile(path):
            os.remove(path)
else:
    os.makedirs(worker_dir)

current_path = os.getcwd()
os.chdir(tsc_dir)
try:
    result = env.Execute("npm run build")
finally:
    os.chdir(current_path)

if result != 0 or not os.path.isfile(bundle):
    print("ERROR: worker build failed. If tsc/node_modules is missing, run "
          "'npm install --ignore-scripts' in the tsc folder first.")
    env.Exit(1)

shutil.copyfile(bundle, os.path.join(worker_dir, "worker.js"))
print("Copied worker to data/www/worker/worker.js")

# mklittlefs (tool-mklittlefs 0.2.3) silently skips any file whose name is longer
# than 32 characters, and then stops adding the rest of that folder - the file ends up
# missing from the image while the web UI still references it. Fail loudly instead.
LITTLEFS_NAME_MAX = 32
data_dir = os.path.join(project_dir, "data")
too_long = []
for root, _dirs, files in os.walk(data_dir):
    for name in files:
        if len(name) > LITTLEFS_NAME_MAX:
            too_long.append(os.path.relpath(os.path.join(root, name), project_dir))
if too_long:
    print("ERROR: these files have names longer than %d characters and would be "
          "silently dropped from the LittleFS image:" % LITTLEFS_NAME_MAX)
    for path in sorted(too_long):
        print("  " + path)
    env.Exit(1)

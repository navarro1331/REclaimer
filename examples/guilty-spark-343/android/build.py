"""Portable build entry point: JVM argv only, no shell, no global installation."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--unit-only', action='store_true')
    args = parser.parse_args()
    java_home = os.environ.get('JAVA_HOME')
    java = str(Path(java_home) / 'bin' / ('java.exe' if os.name == 'nt' else 'java')) if java_home else shutil.which('java')
    javac = str(Path(java_home) / 'bin' / ('javac.exe' if os.name == 'nt' else 'javac')) if java_home else shutil.which('javac')
    if not java or not javac:
        raise SystemExit('Set JAVA_HOME to a JDK 17 or newer, or put java and javac on PATH.')
    classes = ROOT / 'build' / 'connection-tests'
    classes.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'app/src/main/java/com/guiltyspark343/android'
    subprocess.run([javac, '-encoding', 'UTF-8', '-d', str(classes), str(source / 'ConnectionAddress.java'), str(source / 'ServerProbe.java'), str(ROOT / 'tests/ConnectionTests.java')], cwd=ROOT, check=True)
    subprocess.run([java, '-cp', str(classes), 'com.guiltyspark343.android.ConnectionTests'], cwd=ROOT, check=True)
    if args.unit_only:
        return
    wrapper = ROOT / 'gradle/wrapper/gradle-wrapper.jar'
    if not wrapper.exists():
        raise SystemExit('Missing Gradle wrapper jar. Restore the checked-in wrapper before building.')
    command = [java, '-classpath', str(wrapper), 'org.gradle.wrapper.GradleWrapperMain', '--no-daemon']
    if args.offline:
        command.append('--offline')
    command.extend([':app:assembleDebug', ':app:assembleRelease', ':app:assembleDebugAndroidTest', ':app:lintDebug'])
    subprocess.run(command, cwd=ROOT, check=True)

if __name__ == '__main__':
    main()

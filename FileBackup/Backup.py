import os,sys
import time
import hashlib
import tarfile
import cPickle as pickle

'''
Notes:
1) Ensure that the file is correctly closed under all circumstances.
    with open(filename) as fobj:
        fobj.read()
    
2) md5dict, md5old or md5new is Dictionary. 
   Use file's full_name as 'Key' and file's md5 as 'Value'

3) tarfile

4) cPickle (only for Python 2.7) or pickle -- serializing and de-serializing a Python object structure.
'''

# Get all directories and files
# Method_1 Recursive with os.listdir
def lsDir(folder):
    contents = os.listdir(folder)
    for path in contents:
        full_path = os.path.join(folder, path)
        # Travese sub-dirs
        if os.path.isdir(full_path):
            lsDir(full_path)

# Method_2 os.walk
def walkDir(folder):
    contents = os.walk(folder)
    print contents

    for path, folder, file in contents:
        print "%s\t%s\n" % (path, folder+file)

# Compare the files
# Method_1 Use md5 value of the file
def md5(filename):
    if not os.path.isfile(filename):
        print "md5(): invalid file!"
        return

    m = hashlib.md5()
    #Open the file and get the md5 with file's content
    with open(filename) as file:
        while True:
            file_data = file.read(4096)
            if not file_data:
                break
            m.update(file_data)

    return m.hexdigest()

# Method_2 Use the modified timestamp
def getTimestamp(filename):
    stat = os.stat(filename)
    #print stat
    return stat.st_mtime

# Increment backup according to the timestamp of files
def inc_backup_timestamp(src_dir, dst_dir):
    contents = os.listdir(src_dir)
    for fname in contents:
        full_path = os.path.join(src_dir, fname)
        full_path_bak = os.path.join(dst_dir, fname)

        # If file exists in dst_dir
            # If full_path is a directory
            # else compare the timestamp of full_path and full_path_bak (if exists)
        # else recrusive copy to back directory



# Full and Incremental backup to tar.gz file
def full_backup_tar(src_dir, dst_dir, md5file):
    par_dir, base_dir = os.path.split(src_dir.rstrip('/'))
    back_name = '%s_full_%s.tar.gz' % (base_dir, time.strftime('%Y%m%d'))
    full_name = os.path.join(dst_dir, back_name)
    md5dict = {}

    # Tar all files in 'src_dir'
    tar = tarfile.open(full_name, 'w:gz')
    tar.add(src_dir)
    tar.close()

    # Calculate md5 of all files and save into the md5file
    for path, folders, files in os.walk(src_dir):
        for fname in files:
            full_path = os.path.join(path, fname)
            md5dict[full_path] = md5(full_path)

    with open(md5file, 'w') as fobj:
        pickle.dump(md5dict, fobj)


def incr_backup_tar(src_dir, dst_dir, md5file):
    par_dir, base_dir = os.path.split(src_dir.rstrip('/'))
    back_name = '%s_incr_%s.tar.gz' % (base_dir, time.strftime('%Y%m%d'))
    full_name = os.path.join(dst_dir, back_name)
    md5new = {}

    # Get the md5 for all files in 'src_dir'
    for path, folders, files in os.walk(src_dir):
        for fname in files:
            full_path = os.path.join(path, fname)
            md5new[full_path] = md5(full_path)

    # Load old md5 value to md5old before Update the md5 file with new value.
    with open(md5file) as fobj:
        md5old = pickle.load(fobj)

    with open(md5file, 'w') as fobj:
        pickle.dump(md5new, fobj)

    # Add the 'updated' file to *.tar.gz
    tar = tarfile.open(full_name, 'w:gz')
    for key in md5new:
        # if file is changed or not exists in md5old
        if md5old.get(key) != md5new[key]:
            tar.add(key) # 'key' is file's full name
    tar.close()

if __name__ == "__main__":
    '''
    src_dir = '/Users/ChenJu/workspace/'
    dst_dir = '/tmp/'
    md5file = '/Users/ChenJu/md5_workspace.data'
    if time.strftime('%a') == 'Mon':
        full_backup(src_dir, dst_dir, md5file)
    else:
        incr_backup(src_dir, dst_dir, md5file)
    '''

    #full_backup_tar(sys.argv[1], sys.argv[2], sys.argv[3])
    #incr_backup_tar(sys.argv[1], sys.argv[2], sys.argv[3])

    inc_backup_timestamp(sys.argv[1], sys.argv[2])





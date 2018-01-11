import os,sys
import time
import datetime
import hashlib
import tarfile
import cPickle as pickle
import shutil
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

def DebugOut():
    return

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

        '''
        ## Method_1 Check if the dst_file does exist
        # If file exists in dst_dir
        if os.path.exists(full_path_bak):
            # If full_path is a directory
            if os.path.isdir(full_path_bak):
                inc_backup_timestamp(full_path, full_path_bak)

            # compare the timestamp of full_path and full_path_bak (if exists)
            else:
                # st_mtime is float and has 0.000001 diff sometimes for the same file
                if  int(os.stat(full_path).st_mtime - os.stat(full_path_bak).st_mtime):
                    shutil.copy2(full_path, full_path_bak)
                #TODO: what we should do if the dst_file timestamp is newer than src_file timestamp
                
        # else recrusive copy to back directory
        else:
            # shutil.copytree only recrusivly copies the directory
            if os.path.isdir(full_path):
                shutil.copytree(full_path, full_path_bak)
            else:
                shutil.copy2(full_path, full_path_bak)
        '''

        ## Method_2  Check if src file is a directory
        # If full_path is a directory
        if  os.path.isdir(full_path):
            # If back directory does not have this directory, recrusive copy
            if not os.path.exists(full_path_bak):
                shutil.copytree(full_path, full_path_bak)
            else:
                inc_backup_timestamp(full_path, full_path_bak)
        # else full_path is a file
        else:
            # If full_path_back exists then compare the st_mtime
            if os.path.exists(full_path_bak):
                if  int(os.stat(full_path).st_mtime - os.stat(full_path_bak).st_mtime):
                    shutil.copy2(full_path, full_path_bak)
                #TODO: what we should do if the dst_file timestamp is newer than src_file timestamp
            else:
                shutil.copy2(full_path, full_path_bak)


# get the md5 dictionary of src and dst
# use the md5 hexdigital as key and do the comparsion
def inc_backup_md5(src_dir, dst_dir):
    md5src = {}
    md5dst = {}

    #TODO: Not finalizaed
    # using "path" and remove the top-level directory (src_dir)
    # Check missing directory in dst_dir
    # and recrusive copy?
    # recrusive dig into unmiss folder


    #Get the md5 for file in src and dst
    for path, folders, files in os.walk(dst_dir):
        for fname in files:
            full_path_bak = os.path.join(path, fname)
            md5dst[full_path_bak] = md5(full_path_bak)

    for path, folders, files in os.walk(src_dir):
        for fname in files:
            full_path = os.path.join(path, fname)
            #key = full_path.rsplit(src_dir)
            print full_path
            print full_path.rsplit(src_dir)
            #md5src[key] = md5(full_path)

    #Compare the src & dst md5 dictionaries, if not equal, copy src file to dst


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


#TODO:
#func_1 use src_path (dir or file) as arg1 and timestamp position as arg2 (prefix as default)
#       change the file name by adding the timestamp

#func_2 remove the timestamp string in the filename

#func_3 backup favourite files or photos

def change_filename_gmtime(src_path):
    if not os.path.exists(src_path):
        sys.exit('Err: %s does not exists!'%src_path)

    if os.path.isfile(src_path):
        file_mtime = os.stat(src_path).st_mtime
        file_name = os.path.basename(src_path)
        dir_name = os.path.dirname(src_path)

        # Use 'time' module
        #print "time Local: %s"%time.localtime(file_mtime)
        #print "time UTC:   %s"%time.gmtime(file_mtime)

        # Use 'datetime' module
        #print "datetime Local: %s"%datetime.datetime.fromtimestamp(file_mtime)
        #print "datetime UTC:   %s"%datetime.datetime.utcfromtimestamp(file_mtime)

        file_gmtime = time.gmtime(file_mtime)
        file_gmtime_name = "%04d%02d%02d_%02d%02d%02d_%s"%(file_gmtime.tm_year, file_gmtime.tm_mon, file_gmtime.tm_mday,
                                                           file_gmtime.tm_hour, file_gmtime.tm_min, file_gmtime.tm_sec,
                                                           file_name)

        # Debug
        # This could happen if the arg is file instead of directory
        if os.getcwd() != dir_name:
            print "Entering folder %s"%dir_name
            os.chdir(dir_name)

        os.rename(file_name, file_gmtime_name)

        # Debug
        print "Change file name %s to %s" % (file_name, file_gmtime_name)

    else:
        # listdir or walk
        print "Entering folder %s" % src_path
        os.chdir(src_path)

        for f_path in os.listdir(src_path):
            #print os.path.join(src_path, f_path)
            change_filename_gmtime(os.path.join(src_path, f_path))

    return

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

    #inc_backup_timestamp(sys.argv[1], sys.argv[2])

    #inc_backup_md5(sys.argv[1], sys.argv[2])

    change_filename_gmtime(sys.argv[1])


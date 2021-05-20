import subprocess


# run tmux command in the specified session
def run_tmux_command(cmd: str, session_name: str) -> None:

    # define tmux commands
    tmux_create_session = 'tmux new-session -d -s ' + session_name
    tmux_split_window = 'tmux split-window -h -t ' + session_name + '.right'
    tmux_run_cmd = 'tmux send -t ' + session_name + ' "' + cmd + '" ENTER'

    # start new session. This returns 'duplicate session' if session already exists
    result = subprocess.run(tmux_create_session, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # split window if session already existed before,
    if 'duplicate session' in result.stderr.decode("utf-8"):
        subprocess.run(tmux_split_window, shell=True)

    # run command in last created window
    subprocess.run(tmux_run_cmd, shell=True)
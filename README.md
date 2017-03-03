# Command-line client for Log Periodic

Log Periodic is a real-time event logging system. You can learn more at [logperiodic.com](https://logperiodic.com).

This is the source code for the command-line interface to Log Periodic. See our [command-line documentation](https://logperiodic.com/docs#logp-command-line) for more details.


## Pre-built binaries

We have pre-built binaries for 64-bit Debian and RedHat based systems. Please see [our official website](https://logperiodic.com/docs#installation) for details.

Note that our pre-built binaries bundle a copy of OpenSSL so that they are as portable as possible. This is not ideal and is one reason you may wish to compile yourself. If so, keep reading...


## Compilation

First clone the git repo and change into the created directory:

    git clone https://github.com/hoytech/logp.git
    cd logp

Initialize the submodules:

    git submodule update --init

You will need a (recent) C++ compiler and a few other odds and ends. On Debian/Ubuntu systems:

    sudo apt-get install -y build-essential g++ perl libssl-dev

On RedHat/CentOS/Amazon Linux systems:

    sudo yum install -y gcc-c++ perl openssl-devel

Next, run make (using 4 processes speeds it up a bit):

    make -j 4

If all goes well, your binary will be called `logp`. If all does not go well, please [file a github issue](https://github.com/hoytech/logp/issues/new) and don't forget to include the full error message and your OS/compiler types and versions.


## Configuration

In order to use the `logp` client, you first need to get an API key from the Log Periodic dashboard. Then you should put the following config in `~/.logp.conf` or `/etc/logp.conf` (replacing with your own API key of course):

    apikey: zrwoszvidAGW09sfiCr3Ii-YIvyeW1oxEMv8lKgyJVhW3

For full details on configuring the client, please see [our online documentation](https://logperiodic.com/docs#configure).


## Usage

In order to record an event, use the `logp run` command. For example:

    $ logp run my-command --options
    [output from my-command]

You can see a list of currently running jobs with `logp ps`:

    $ logp ps
    EVID    START   USER@HOSTNAME           PID     CMD
    479     11s     doug@www02              21261   my-command --options

Or follow a tail of job activity with `logp ps -f`:

    $ logp ps -f
    [Feb24 14:41:49] │          +  my-command --options  (doug@www02 evid=479 pid=21261)
    [Feb24 14:43:08] │┬         +  sleep 5  (doug@dev01 evid=480 pid=21267)
    [Feb24 14:43:09] ││┬        +  sleep 10  (doug@dev01 evid=481 pid=21269)
    [Feb24 14:43:13] │┴│        -    ✓  (evid=480 runtime=5s)
    [Feb24 14:43:15] │ ┴        -    ✗ killed by Interrupt  (evid=481 runtime=6s)


## Contact

For help, please don't hesitate to [contact us](https://logperiodic.com/contact).

Questions/bug reports/feature requests? [File an issue](https://github.com/hoytech/logp/issues/new)!


## License

This software is copyright Log Periodic Ltd.

It is licensed under the GNU GPL version 3. See the `COPYING` file for details.

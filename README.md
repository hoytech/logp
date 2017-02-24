# Command-line client for Log Periodic

Log Periodic is a real-time event logging system. You can learn more at [logperiodic.com](https://logperiodic.com).


## Compilation

On debian/ubuntu systems:

    sudo apt-get install build-essential g++ perl libssl-dev
    git submodule update --init
    make -j 4


## Configuration

In order to use the `logp` client, you first need to get an API key from the Log Periodic dashboard. Then you should put the following config in `~/.logp` or `/etc/logp.conf` :

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

For help, please don't hesitate to [contact us](https://logperiodic.com/contact)!

Questions/bug reports/feature requests are also welcome on the [logp github repo](https://github.com/hoytech/logp/issues).


## License

This software is copyright Log Periodic Ltd.

It is licensed under the GNU GPL version 3. See the `COPYING` file for details.

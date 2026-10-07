# webdfu (vendored)

`dfu.js` and `dfuse.js` from <https://github.com/devanlai/webdfu>,
directory `dfu-util/`, commit `56d5d1d961587aca381a1ce97acbc8ff5d807acd`
(2021-06-26). ISC license, see `LICENSE` beside this file.

One local change, in `dfuse.js` `dfuseCommand()`: upstream's failure
message referenced an undefined `commandName`, so a failed DfuSe command
threw a `ReferenceError` instead of saying which command failed. It now
reads `commandNames[command]`.

Used by `../../flash.html` through `../../flash-core.js`.

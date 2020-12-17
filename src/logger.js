class Logger {
  constructor(module) {
    this.module = module
  }

  log(line) {
    console.log('[' + this.currentTimeStr() + '][' + this.module + ']' + line)
  }

  logError(line) {
    console.log(
      '[' + this.currentTimeStr() + '][' + this.module + '][ER] ' + line
    )
  }

  logInfo(line) {
    console.log(
      '[' + this.currentTimeStr() + '][' + this.module + '][IF] ' + line
    )
  }

  logDebug(line) {
    console.log(
      '[' + this.currentTimeStr() + '][' + this.module + '][DT] ' + line
    )
  }

  currentTimeStr() {
    const now = new Date(Date.now())
    const year = now.getFullYear()
    const month = now.getMonth() + 1
    const day = now.getDate()
    const hour = now.getHours()
    const min = now.getMinutes()
    const sec = now.getSeconds()
    const ms = now.getMilliseconds()
    return (
      year +
      '-' +
      month +
      '-' +
      day +
      ' ' +
      hour +
      ':' +
      min +
      ':' +
      sec +
      ':' +
      ms
    )
  }
}

export default Logger

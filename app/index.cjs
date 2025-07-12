const { app, shell, BrowserWindow, ipcMain, desktopCapturer, session } = require('electron')
const path = require('path')

const isDev = process.env.DEV != undefined;
let mainWindow;

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1286,
        height: 844 + 32,
        autoHideMenuBar: true,
        frame: true,
        webPreferences: {
            preload: path.join(__dirname, 'preload.cjs'),
            spellcheck: false,
            nodeIntegration: true,// for allowing preload js to use node api
            contextIsolation: true,
        }
    })


    ipcMain.on('minimize-window', () => {
        mainWindow.minimize();
    });
    ipcMain.on('maximize-window', () => {
        if (mainWindow.isMaximized()) {
            mainWindow.unmaximize();
        } else {
            mainWindow.maximize();
        }
    });
    ipcMain.on('close-window', () => {
        mainWindow.close();
    });

    mainWindow.on('ready-to-show', () => {
        mainWindow.show()
    })

    mainWindow.on('closed', () => {
        if (process.platform !== 'darwin') {
            app.quit()
        }
    })

    // accept all certificates in this window
    mainWindow.webContents.on('certificate-error', (event, url, error, certificate, callback) => {
        event.preventDefault()
        callback(true)
    })


    if (isDev) {
        mainWindow.loadURL('http://localhost:5173')// load vite dev server
        mainWindow.webContents.openDevTools({ mode: 'detach' })
    } else {
        mainWindow.loadFile(path.join(__dirname, '../dist/index.html'))
        mainWindow.webContents.openDevTools({ mode: 'detach' })
    }
}

app.whenReady().then(() => {

    createWindow()

    app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) {
            createWindow()
        }
    })
})

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') {
        app.quit()
    }
})
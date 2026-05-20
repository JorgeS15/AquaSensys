const translations = {
  en: {
    // Navigation
    "dashboard": "Dashboard",
    "diagnostics": "Diagnostics",
    "settings": "Settings",
    "fileUpload": "File Upload",
    "connecting": "Connecting...",
    
    // Dashboard cards
    "pressure": "Pressure",
    "temperature": "Temperature",
    "flowRate": "Flow Rate",
    "systemControls": "System Controls",
    "systemInfo": "System Info",
    
    // Units
    "bar": "bar",
    "celsius": "°C",
    "lpm": "L/min",
    
    // Target
    "target": "Target",
    
    // Status labels
    "motor": "Motor",
    "mode": "Mode",
    "system": "System",
    "error": "Error",
    "on": "ON",
    "off": "OFF",
    "auto": "AUTO",
    "manual": "MANUAL",
    "yes": "YES",
    "no": "NO",
    
    // Controls
    "toggleMotor": "Toggle Motor",
    "manualOverride": "Manual Override",
    "turnOn": "Turn ON",
    "turnOff": "Turn OFF",
    "rebootDevice": "Reboot Device",
    
    // System info
    "uptime": "Uptime",
    "freeMemory": "Free Memory",
    "wifiSignal": "WiFi Signal",
    "lastReset": "Last Reset",
    
    // Settings page - Network
    "networkSettings": "Network Settings",
    "wifiSsid": "WiFi SSID:",
    "wifiPassword": "WiFi Password:",
    "leaveBlankPassword": "Leave blank to keep current password",
    "saveNetworkSettings": "Save Network Settings",
    
    // Settings page - MQTT
    "mqttSettings": "MQTT Settings",
    "mqttServer": "MQTT Server:",
    "mqttPort": "MQTT Port:",
    "mqttUsername": "MQTT Username:",
    "mqttPassword": "MQTT Password:",
    
    // Settings page - System Parameters
    "systemParameters": "System Parameters",
    "minPressureBar": "Minimum Pressure (bar):",
    "maxPressureBar": "Maximum Pressure (bar):",
    "pressureOffset": "Pressure Offset:",
    "tempOffset": "Temperature Offset:",
    "saveParameters": "Save Parameters",
    
    // Settings page - System Actions
    "systemActions": "System Actions",
    "factoryReset": "Factory Reset",
    "firmwareUpdate": "Firmware Update",
    
    // Update page
    "important": "Important",
    "updateWarning": "Do not power off or disconnect the device during the update process. The update may take several minutes to complete.",
    "currentVersion": "Current Version",
    "device": "Device",
    "freeSpace": "Free Space",
    "lastUpdate": "Last Update",
    "checking": "Checking...",
    "unknown": "Unknown",
    "selectFirmwareFile": "Select Firmware File",
    "uploadInstruction": "Click to browse or drag and drop your .bin file here",
    "startUpdate": "Start Update",
    "checkSDCard": "Check SD Card",
    "uploaded": "Uploaded",
    "speed": "Speed",
    "timeElapsed": "Time Elapsed",
    "timeRemaining": "Time Remaining",
    "checkingSDCard": "Checking for update file on SD card...",
    "updateFileFound": "Update file found on SD card! Click to apply update.",
    "noUpdateFile": "No update file found on SD card.",
    "checkUpdateError": "Failed to check for updates",
    "applyUpdateFromSD": "Apply Update from SD",
    "confirmSDUpdate": "Apply firmware update from SD card? The device will restart after update.",
    "applyingSDUpdate": "Applying update from SD card...",
    "applyUpdateFailed": "Failed to apply update",
    "confirmUpdate": "Start firmware update? The device will restart after successful update.",
    "uploadingFirmware": "Uploading firmware...",
    "updateSuccessful": "Update successful! Device will restart in 5 seconds...",
    "updateFailed": "Update failed",
    "waitingRestart": "Waiting for device to restart...",
    "updateAppearSuccessful": "Update appears successful! Device is restarting...",
    "uploadFailedNetwork": "Upload failed: Network error",
    "updateCancelled": "Update cancelled",
    "deviceOnline": "Device is back online! Redirecting...",
    "deviceNotOnline": "Device did not come back online. Please check the device.",
    
    // Upload page
    "sdCardFileManager": "SD Card File Manager",
    "instructions": "Instructions",
    "uploadInstructions": "Select your web files (index.html, app.js, style.css, etc.) and upload them to the SD card root directory. This will make them available for your web interface.",
    "uploadFilesToSD": "Upload Files to SD Card",
    "selectMultipleFiles": "Select multiple files to upload to the SD card root directory",
    "uploadSelectedFiles": "Upload Selected Files",
    "backToDashboard": "Back to Dashboard",
    "refreshFileList": "Refresh File List",
    "debugInfo": "Debug Info",
    "filesOnSDCard": "Files on SD Card:",
    "loadingFiles": "Loading files...",
    "pleaseSelectFiles": "Please select files to upload",
    "uploadingFiles": "Uploading %d file(s)...",
    "uploadFailed": "Upload failed for %s",
    "errorUploading": "Error uploading %s",
    "successfullyUploaded": "Successfully uploaded %d file(s)!",
    "confirmDelete": "Are you sure you want to delete \"%s\"?",
    "fileDeleted": "File deleted",
    "errorDeletingFile": "Error deleting file",
    "noFilesFound": "No files found on SD card",
    "delete": "Delete",
    "summary": "Summary",
    "files": "files",
    "total": "total",
    "errorLoadingFileList": "Error loading file list",
    "fileManagerLoaded": "File manager loaded successfully!",
    "filesSelectedDragDrop": "%d file(s) selected via drag & drop",
    
    // Notifications
    "configSaved": "Configuration saved successfully",
    "wifiUpdated": "WiFi settings updated. Device will reconnect if needed.",
    "mqttUpdated": "MQTT settings updated. Connection will be reestablished.",
    "factoryResetConfirm": "Are you sure you want to reset all settings to factory defaults? This will erase all your configuration.",
    "factoryResetSuccess": "Factory reset successful. Device will reboot.",
    "rebootConfirm": "Are you sure you want to reboot the device?",
    "rebootMessage": "Device will reboot shortly",
    "loadConfigError": "Failed to load configuration",
    "saveConfigError": "Failed to save configuration",
    "rebootError": "Failed to reboot device",
    "factoryResetError": "Failed to perform factory reset",
    
    // Page titles
    "mainTitle": "AquaSensys",
    "settingsTitle": "AquaSensys - Settings",
    
    // Language
    "language": "Language",
    "english": "English",
    "portuguese": "Portuguese"
  },
  pt: {
    // Navigation
    "dashboard": "Painel",
    "diagnostics": "Diagnósticos",
    "settings": "Configurações",
    "fileUpload": "Envio de Ficheiros",
    "connecting": "A conectar...",
    
    // Dashboard cards
    "pressure": "Pressão",
    "temperature": "Temperatura",
    "flowRate": "Fluxo",
    "systemControls": "Controlos do Sistema",
    "systemInfo": "Informações do Sistema",
    
    // Units
    "bar": "bar",
    "celsius": "°C",
    "lpm": "L/min",
    
    // Target
    "target": "Alvo",
    
    // Status labels
    "motor": "Motor",
    "mode": "Modo",
    "system": "Sistema",
    "error": "Erro",
    "on": "ATIVO",
    "off": "INATIVO",
    "auto": "AUTO",
    "manual": "MANUAL",
    "yes": "SIM",
    "no": "NÃO",
    
    // Controls
    "toggleMotor": "Alternar Motor",
    "manualOverride": "Controlo Manual",
    "turnOn": "Ligar",
    "turnOff": "Desligar",
    "rebootDevice": "Reiniciar Dispositivo",
    
    // System info
    "uptime": "Tempo de Atividade",
    "freeMemory": "Memória Livre",
    "wifiSignal": "Sinal WiFi",
    "lastReset": "Último Reinício",
    
    // Settings page - Network
    "networkSettings": "Configurações de Rede",
    "wifiSsid": "WiFi SSID:",
    "wifiPassword": "Palavra-passe WiFi:",
    "leaveBlankPassword": "Deixe em branco para manter a palavra-passe atual",
    "saveNetworkSettings": "Guardar Configurações de Rede",
    
    // Settings page - MQTT
    "mqttSettings": "Configurações MQTT",
    "mqttServer": "Servidor MQTT:",
    "mqttPort": "Porta MQTT:",
    "mqttUsername": "Nome de Utilizador MQTT:",
    "mqttPassword": "Palavra-passe MQTT:",
    
    // Settings page - System Parameters
    "systemParameters": "Parâmetros do Sistema",
    "minPressureBar": "Pressão Mínima (bar):",
    "maxPressureBar": "Pressão Máxima (bar):",
    "pressureOffset": "Offset de Pressão:",
    "tempOffset": "Offset de Temperatura:",
    "saveParameters": "Guardar Parâmetros",
    
    // Settings page - System Actions
    "systemActions": "Ações do Sistema",
    "factoryReset": "Restaurar Configurações de Fábrica",
    "firmwareUpdate": "Atualização de Firmware",
    
    // Update page
    "important": "Importante",
    "updateWarning": "Não desligue ou desconecte o dispositivo durante o processo de atualização. A atualização pode demorar alguns minutos a ser concluída.",
    "currentVersion": "Versão Atual",
    "device": "Dispositivo",
    "freeSpace": "Espaço Livre",
    "lastUpdate": "Última Atualização",
    "checking": "A verificar...",
    "unknown": "Desconhecido",
    "selectFirmwareFile": "Selecionar Ficheiro de Firmware",
    "uploadInstruction": "Clique para procurar ou arraste e largue o seu ficheiro .bin aqui",
    "startUpdate": "Iniciar Atualização",
    "checkSDCard": "Verificar Cartão SD",
    "uploaded": "Enviado",
    "speed": "Velocidade",
    "timeElapsed": "Tempo Decorrido",
    "timeRemaining": "Tempo Restante",
    "checkingSDCard": "A verificar ficheiro de atualização no cartão SD...",
    "updateFileFound": "Ficheiro de atualização encontrado no cartão SD! Clique para aplicar atualização.",
    "noUpdateFile": "Nenhum ficheiro de atualização encontrado no cartão SD.",
    "checkUpdateError": "Falha ao verificar atualizações",
    "applyUpdateFromSD": "Aplicar Atualização do SD",
    "confirmSDUpdate": "Aplicar atualização de firmware do cartão SD? O dispositivo irá reiniciar após a atualização.",
    "applyingSDUpdate": "A aplicar atualização do cartão SD...",
    "applyUpdateFailed": "Falha ao aplicar atualização",
    "confirmUpdate": "Iniciar atualização de firmware? O dispositivo irá reiniciar após atualização bem-sucedida.",
    "uploadingFirmware": "A enviar firmware...",
    "updateSuccessful": "Atualização bem-sucedida! O dispositivo irá reiniciar em 5 segundos...",
    "updateFailed": "Atualização falhada",
    "waitingRestart": "A aguardar reinício do dispositivo...",
    "updateAppearSuccessful": "A atualização parece ter sido bem-sucedida! O dispositivo está a reiniciar...",
    "uploadFailedNetwork": "Falha no envio: Erro de rede",
    "updateCancelled": "Atualização cancelada",
    "deviceOnline": "Dispositivo está online! A redirecionar...",
    "deviceNotOnline": "O dispositivo não ficou online. Por favor, verifique o dispositivo.",
    
    // Upload page
    "sdCardFileManager": "Gestor de Ficheiros do Cartão SD",
    "instructions": "Instruções",
    "uploadInstructions": "Selecione os seus ficheiros web (index.html, app.js, style.css, etc.) e envie-os para o diretório raiz do cartão SD. Isto irá torná-los disponíveis para a sua interface web.",
    "uploadFilesToSD": "Enviar Ficheiros para o Cartão SD",
    "selectMultipleFiles": "Selecione múltiplos ficheiros para enviar para o diretório raiz do cartão SD",
    "uploadSelectedFiles": "Enviar Ficheiros Selecionados",
    "backToDashboard": "Voltar ao Painel",
    "refreshFileList": "Atualizar Lista de Ficheiros",
    "debugInfo": "Informações de Debug",
    "filesOnSDCard": "Ficheiros no Cartão SD:",
    "loadingFiles": "A carregar ficheiros...",
    "pleaseSelectFiles": "Por favor selecione ficheiros para enviar",
    "uploadingFiles": "A enviar %d ficheiro(s)...",
    "uploadFailed": "Falha no envio de %s",
    "errorUploading": "Erro ao enviar %s",
    "successfullyUploaded": "Enviado com sucesso %d ficheiro(s)!",
    "confirmDelete": "Tem a certeza que deseja eliminar \"%s\"?",
    "fileDeleted": "Ficheiro eliminado",
    "errorDeletingFile": "Erro ao eliminar ficheiro",
    "noFilesFound": "Nenhum ficheiro encontrado no cartão SD",
    "delete": "Eliminar",
    "summary": "Resumo",
    "files": "ficheiros",
    "total": "total",
    "errorLoadingFileList": "Erro ao carregar lista de ficheiros",
    "fileManagerLoaded": "Gestor de ficheiros carregado com sucesso!",
    "filesSelectedDragDrop": "%d ficheiro(s) selecionado(s) via arrastar e largar",
    
    // Notifications
    "configSaved": "Configuração guardada com sucesso",
    "wifiUpdated": "Configurações WiFi atualizadas. O dispositivo reconectar-se-á se necessário.",
    "mqttUpdated": "Configurações MQTT atualizadas. A ligação será restabelecida.",
    "factoryResetConfirm": "Tem a certeza que deseja repor todas as configurações para os valores de fábrica? Isto apagará toda a sua configuração.",
    "factoryResetSuccess": "Reposição de fábrica bem-sucedida. O dispositivo vai reiniciar.",
    "rebootConfirm": "Tem a certeza que deseja reiniciar o dispositivo?",
    "rebootMessage": "O dispositivo vai reiniciar em breve",
    "loadConfigError": "Falha ao carregar configuração",
    "saveConfigError": "Falha ao guardar configuração",
    "rebootError": "Falha ao reiniciar dispositivo",
    "factoryResetError": "Falha ao restaurar configurações de fábrica",
    
    // Page titles
    "mainTitle": "AquaSensys",
    "settingsTitle": "AquaSensys - Configurações",
    
    // Language
    "language": "Idioma",
    "english": "Inglês",
    "portuguese": "Português"
  }
};
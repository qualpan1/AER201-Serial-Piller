/* 
 * File:   main.c
 * Author: Daniil Orekhov
 *
 * Created on January 13, 2018, 3:27 PM
 */

#include "main.h"

Operation operations[8];
unsigned int operationNum;
volatile unsigned int currentMachineMode = MODE_STANDBY;

/* Initialize local variables. */
unsigned char mem[3]; // Initialize array to check for triple-A sequence
unsigned char counter = 0; // Increments each time a byte is sent
unsigned char keypress; // Stores the data corresponding to the last key press
unsigned char data; // Holds the data to be sent/received

volatile unsigned int rTotalCount = 0;
volatile unsigned int fTotalCount = 0;
volatile unsigned int lTotalCount = 0;

volatile unsigned int rPassedRecently = 0;
volatile unsigned int fPassedRecently = 0;
volatile unsigned int lPassedRecently = 0;

volatile unsigned int secondsWithoutR = 0;
volatile unsigned int secondsWithoutF = 0;
volatile unsigned int secondsWithoutL = 0;

volatile unsigned int runTime = 0;
volatile unsigned int rTime = 0;
volatile unsigned int fTime = 0;
volatile unsigned int lTime = 0;

void main(void) {
    
    // <editor-fold defaultstate="collapsed" desc="Machine Configuration">
    OSCCON = 0xF2;  // Use 8 MHz internal oscillator block

    /********************************* PIN I/O ********************************/
    /* Write outputs to LATx, read inputs from PORTx. Here, all latches (LATx)
     * are being cleared (set low) to ensure a controlled start-up state. */  
    LATA = 0x00;
    LATB = 0x00; 
    LATC = 0x00;
    LATD = 0x00;
    LATE = 0x00;

    /* After the states of LATx are known, the data direction registers, TRISx
     * are configured. 0 --> output; 1 --> input. Default is  1. */
    TRISA = 0xFF; // All inputs (this is the default, but is explicated here for learning purposes)
    TRISB = 0xFF;
    TRISC = 0x00;
    TRISD = 0x00; // All output mode on port D for the LCD
    TRISE = 0x00;
    
    /************************** A/D Converter Module **************************/
    ADCON0 = 0x00;  // Disable ADC
    ADCON1 = 0x0C;  // RA0-3 are analog (pg. 222)
    ADCON2bits.ADFM = 1; // Right justify A/D result
    
    /***************************** Timer 0 Module *****************************/
    T0CONbits.T08BIT = 0;
    T0CONbits.T0CS = 0;
    T0CONbits.PSA = 0;
    T0CONbits.T0PS2 = 0;
    T0CONbits.T0PS1 = 0;
    T0CONbits.T0PS0 = 1;
    TMR0H = 0x3C;
    TMR0L = 0xB0;
    T0CONbits.TMR0ON = 1;
    TMR0IE = 1;
    
    // </editor-fold>
    
    // <editor-fold defaultstate="collapsed" desc="Initialize EUSART module for asynchronous 9600/8N1">
    /* Configure the baud rate generator for 9600 bits per second. */
    long baudRate = 9600;
    SPBRG = (unsigned char)((_XTAL_FREQ / (64 * baudRate)) - 1);
    
    /* Configure transmit control register */
    TXSTAbits.TX9 = 0; // Use 8-bit transmission (8 data bits, no parity bit)
    TXSTAbits.SYNC = 0; // Asynchronous communication
    TXSTAbits.TXEN = 1; // Enable transmitter
    __delay_ms(5); // Enabling the transmitter requires a few CPU cycles for stability
    
    /* Configure receive control register */
    RCSTAbits.RX9 = 0; // Use 8-bit reception (8 data bits, no parity bit)
    RCSTAbits.CREN = 1; // Enable receiver
    
    /* Enforce correct pin configuration for relevant TRISCx */
    TRISC6 = 0; // TX = output
    TRISC7 = 1; // RX = input
    
    /* Enable serial peripheral */
    RCSTAbits.SPEN = 1;
    // </editor-fold>
    
    INT1IE = 1; // Enable RB1 (keypad data available) interrupt
    ei(); // Enable all interrupts
    
    initLCD();
    
    I2C_Master_Init(100000);
    
    TRISCbits.RC0 = 1;
    TRISCbits.RC1 = 1;
    TRISCbits.RC2 = 1;
    
    TRISCbits.RC5 = 0;
    LATCbits.LATC5 = 0;
       
    if (read_octet_eep(1) == 255) cleanEEPROM();
    
    while (1) {
        
        if (currentMachineMode == MODE_STANDBY) enterStandbyMode();
            
        if (currentMachineMode == MODE_INPUT) {
            if (getInputMode() == MODE_EMPTY_INPUT) askForOperationInput();
            else if (getInputMode() == MODE_INPUT_DESTINATION) askForDestination();
            else if (getInputMode() == MODE_INPUT_DIET) askForDietType();
            else if (getInputMode() == MODE_INPUT_DIET_NUM) askForDietNum();
            else if (getInputMode() == MODE_INPUT_PROMPT) showPrompt();
            else if (getInputMode() == MODE_SHOW_INPUT) {
                int opNum = getDisplayedOperationNum();
                Operation operation = operations[opNum];
                showInput(opNum, DIETS[operation.dietType - 1], operation.dietNum, operation.destination);
            }
        }
        
        if (currentMachineMode == MODE_RUNNING) {
            runTime = 0;
            __lcd_display_control(1, 0, 0);
            recordStartTime();
            
            INT0IE = 1; // Enable RB0 (keypad data available) interrupt
            INT1IE = 1; // Enable RB1 (keypad data available) interrupt
            INT2IE = 1; // Enable RB2 (keypad data available) interrupt
            
            INTCON2bits.INTEDG0 = 1;  // rising edge
            INTCON2bits.INTEDG1 = 1;  // rising edge
            INTCON2bits.INTEDG2 = 1;  // rising edge
            
            __delay_ms(100);
            
            rTotalCount = 0;
            fTotalCount = 0;
            lTotalCount = 0;
            
            secondsWithoutR = 0;
            secondsWithoutF = 0;
            secondsWithoutL = 0;
            
            runTime = 0;
            rTime = 0;
            fTime = 0;
            lTime = 0;
            
            startTopCounters();  
            
            optimizeOperationOrder(getOperationNum());        
            
            int operationsExecuted = 0;
            int currentColumn = 0;
            
            Operation o = operations[operationsExecuted];
            while (operationsExecuted != operationNum) {
                updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                if (currentColumn != 0) {
                    __lcd_clear();
                    printf("Moving to ");
                    __lcd_newline();
                    printf("column %d", currentColumn+1);
                    moveToColumn(currentColumn*2);
                    updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                }
                if (columnNeedsFood(currentColumn, operationsExecuted)) {
                    __lcd_clear();
                    printf("Opening drawers");
                    __lcd_newline();
                    printf("column %d", currentColumn+1);
                    updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                    openAllDrawers();
                    moveToColumn(currentColumn*2 + 1);
                    updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                    int currentRow = 0;
                    while (currentRow < 4) {
                        updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                        if (rowNeedsFood(currentRow, operationsExecuted)) {
                            updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                            if (!drawerIsTaped(currentRow)) {
                                __lcd_clear();
                                printf("Dispensing:");
                                __lcd_newline();
                                printf("%sx%d into %d", DIETS[o.dietType-1], o.dietNum, o.destination);
                                determineFoodCount(DIETS[o.dietType-1], o.dietNum);
                                updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                                countFood();
                                updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                                __delay_ms(1000);
                            }
                            else operations[operationsExecuted].destination += 100;
                            operationsExecuted = operationsExecuted + 1;
                            o = operations[operationsExecuted];
                            updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                        }
                        updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                        __lcd_clear();
                        printf("Closing drawer");
                        __lcd_newline();
                        printf("row %d", currentRow+1);
                        closeDrawer(currentRow);
                        currentRow = currentRow+1;
                        updateCountAndTime(rTotalCount, fTotalCount, lTotalCount);
                    }
                }
                currentColumn = currentColumn + 1;
            }
            
            __lcd_clear();
            printf("Moving cart");
            __lcd_newline();
            printf("to base");
            
            moveToColumn(0);

            __lcd_clear();
            printf("Waiting for");
            __lcd_newline();
            printf("counters");

            while (!(secondsWithoutR > 10 && secondsWithoutF > 10 && secondsWithoutL > 10));
            
            stopTopCounters();
            
            __lcd_clear();
            printf("Operation");
            __lcd_newline();
            printf("complete!");
            
            currentMachineMode = MODE_LOGS;
            setLogsMode(MODE_OPERATIONS_COMPLETE);
            
            INT0IE = 0;
            INT1IE = 1;
            INT2IE = 0;
            LATCbits.LATC5 = 0; //enable keypad
            
            for (unsigned int i=0; i<operationNum; i++) {
                prepareBuffer(operations[i].destination);
                prepareBuffer(operations[i].dietType);
                prepareBuffer(operations[i].dietNum);
            }
            
            setArduinoToDisplayLogs(getStartTime(), operationNum, rTotalCount, fTotalCount, lTotalCount, runTime);
            
            setFromStandby(0);
            setLogsSaved(0);
        }
        
        if (currentMachineMode == MODE_LOGS) {
            if (getLogsMode() == MODE_OPERATIONS_COMPLETE) showOperationsComplete();
            else if (getLogsMode() == MODE_SAVE_OPERATIONS) showSaveOperations();
            else if (getLogsMode() == MODE_LOGS_PROMPT) showLogsPrompt();
            else if (getLogsMode() == MODE_LOGGING) {
                showLogging(); 
                logCreatedOperations(getOperationNum());
                logCounts(rTotalCount, fTotalCount, lTotalCount);
                logRunTime(runTime);
                completeLogs();
            }
            else if (getLogsMode() == MODE_LOGGING_COMPLETE) showLoggingComplete();
            else if (getLogsMode() == MODE_VIEW_LOGS) {
                if (getCurrentAddress() == 1) {
                    __lcd_clear();
                    printf("No logs ");
                    __lcd_newline();
                    printf("saved in EEPROM.");
                    __delay_ms(1000);
                    setLogsMode(MODE_RETURN);
                }
                else showViewLogs();
            }
            else if (getLogsMode() == MODE_VIEW_LOGS_GLCD) showViewLogsGLCD();
            else if (getLogsMode() == MODE_TRANSFER_LOGS) showTransferLogs();
            else if (getLogsMode() == MODE_TRANSFERRING_LOGS) {
                showTransferringLogs();
                sendLogsToPC();
                completeTransfer();
            }
            else if (getLogsMode() == MODE_RETURN) currentMachineMode = MODE_STANDBY;
        }
    }
}

void enterStandbyMode(void) {
    setTime(0);
    setOperationNum(0);
    setArduinoToStandby();
    __lcd_clear();
    __lcd_display_control(1, 0, 0);
    printf("Welcome!");
    
    int time = 0;
    while (currentMachineMode == MODE_STANDBY) {
        if (readTimer() - time >= 10)  {
            time = readTimer();
            printTimeToGLCD();
        }
    }
}

void createOperation(void) { 
    operations[getOperationNum()].destination = getNewDestination();
    operations[getOperationNum()].dietNum = getNewDietNum();
    operations[getOperationNum()].dietType = getNewDietType();
    setOperationNum(getOperationNum() + 1);
    setOperationReady(0);
}

void deleteOperation(int num) {
    for (int i = num; i<getOperationNum(); i++) {
        operations[i].destination = operations[i+1].destination;
        operations[i].dietNum = operations[i+1].dietNum ;
        operations[i].dietType = operations[i+1].dietType;
    }
    setOperationNum(getOperationNum() - 1);
    setDeleteOperation(0);
}

int discardOperations(int* tapedDrawers, int tapedDrawersNum) {
    int operationNum = getOperationNum();
    for (int i=0; i<operationNum; i++) {
        for (int j = 0; j < tapedDrawersNum; j++) {
            if (operations[i].destination == tapedDrawers[j]) {
                operations[i].destination = operations[i+1].destination;
                operations[i].dietNum = operations[i+1].dietNum ;
                operations[i].dietType = operations[i+1].dietType;
                i--;
                operationNum--;
                break;
            }
        }
    }
    return operationNum;
}

void optimizeOperationOrder(int operationNum){
    int accountedOperationNum = 0;
    int order[] = {1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15, 4, 8, 12, 16};
    
    for (int i=0; i<16; i++) {
        for (int j = accountedOperationNum; j<operationNum; j++) {
            if (operations[j].destination == order[i]) {
                Operation temp = operations[accountedOperationNum];
                operations[accountedOperationNum] = operations[j];
                operations[j] = temp;
                accountedOperationNum++;
            }
        }
    }
}

int columnNeedsFood(int column, int operationsExecuted) {
    int dest = operations[operationsExecuted].destination;
    
    if (dest == 1 || dest == 5 || dest == 9 || dest == 13) return column == 0;
    else if (dest == 2 || dest == 6 || dest == 10 || dest == 14) return column == 1;
    else if (dest == 3 || dest == 7 || dest == 11 || dest == 15) return column == 2;
    else if (dest == 4 || dest == 8 || dest == 12 || dest == 16) return column == 3;
    return 0;
}

int rowNeedsFood(int row, int operationsExecuted) {
    int dest = operations[operationsExecuted].destination;
    
    if (dest == 1 || dest == 2 || dest == 3 || dest == 4) return row == 0;
    else if (dest == 5 || dest == 6 || dest == 7 || dest == 8) return row == 1;
    else if (dest == 9 || dest == 10 || dest == 11 || dest == 12) return row == 2;
    else if (dest == 13 || dest == 14 || dest == 15 || dest == 16) return row == 3;
    
    return 0;
}

void logCreatedOperations(int operationNum){
    logOperationNum(operationNum);
    logStartTime();
    
    for (int i=0; i<operationNum; i++) {
        Operation o = operations[i];        
        logOperation(o.destination, o.dietType, o.dietNum);
    }
}

void interrupt interruptHandler(void){
    if (LATCbits.LATC5 == 0 && INT1IF == 1) {
        unsigned char keypress = (PORTB & 0xF0) >> 4;
        if (currentMachineMode == MODE_STANDBY) {
            if (keypress == 3) {
                currentMachineMode = MODE_INPUT;
                setInputMode(MODE_EMPTY_INPUT);
            }
            else if (keypress == 14) {
                currentMachineMode = MODE_LOGS;
                setLogsMode(MODE_VIEW_LOGS);
                setFromStandby(1);
            }
        }
        else if (currentMachineMode == MODE_INPUT) {
            processInputInterrupt(keypress);
            if (getInputMode() == MODE_NO_INPUT) currentMachineMode = MODE_STANDBY;
            if (isOperationReady()) createOperation();
            if (needToDeleteOperation()) {
                deleteOperation(getDisplayedOperationNum());
                setDisplayedOperationNum(0);
            }
            if (getInputMode() == MODE_INPUT_COMPLETE) {
                LATCbits.LATC5 = 1; //disable keypad
                currentMachineMode = MODE_RUNNING;
                operationNum = getOperationNum();
            }
        }
        else if (currentMachineMode == MODE_LOGS) {
            processLogsInterrupt(keypress);
            if (getLogsMode() == MODE_RETURN) currentMachineMode = MODE_STANDBY;
        }
        INT1IF = 0;
    }
    if (LATCbits.LATC5 == 1) {
        if (INT0IF == 1) {   
            INT0IF = 0;
            if(readTimer() - rTime > 6) {
                rTime = readTimer();
                rTotalCount++;
                rPassedRecently = 1;
                secondsWithoutR = 0;
            }
        }
        if (INT1IF == 1) {
            INT1IF = 0;
            if (readTimer() - fTime > 6) {
                fTime = readTimer();
                fTotalCount++;
                fPassedRecently = 1;
                secondsWithoutF = 0;
            }
        }
        if (INT2IF == 1){
            INT2IF = 0;
            if (readTimer() - lTime > 6) {
                lTime = readTimer();
                lTotalCount++;
                lPassedRecently = 1;
                secondsWithoutL = 0;
            }
        }
    }
    if (TMR0IF == 1) {  
        TMR0H = 0x3C;
        TMR0L = 0xB0;
        TMR0IF = 0;
        increaseTimeCount();
        if (readTimer() % 10 == 0) {
            if (currentMachineMode == MODE_RUNNING) {
                runTime++;
                if (!rPassedRecently) secondsWithoutR++;
                if (!fPassedRecently) secondsWithoutF++;
                if (!lPassedRecently) secondsWithoutL++;

                rPassedRecently = 0;
                fPassedRecently = 0;
                lPassedRecently = 0;
            }
        }
    } 
}

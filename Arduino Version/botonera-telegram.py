import serial
import logging
from telegram import Update
from telegram.ext import Updater, CommandHandler, CallbackContext

# Set up logging
logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.INFO)
logger = logging.getLogger(__name__)

# Serial port configuration
SERIAL_PORT = '/dev/ttyUSB0'  # Change to your Arduino serial port (e.g., "COM3" on Windows)
BAUD_RATE = 9600

# Open serial communication with the Arduino
ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

def send_command_to_arduino(command: str):
    """
    Sends the specified command to the Arduino via serial.
    The command is terminated with a newline character.
    """
    ser.write((command + '\n').encode())
    logger.info(f"Sent command '{command}' to Arduino")

# Telegram command handlers
def vib(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("vib")
    update.message.reply_text("Sent vib command to Arduino.")

def son(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("son")
    update.message.reply_text("Sent son command to Arduino.")

def nivel1(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("nivel1")
    update.message.reply_text("Sent nivel1 command to Arduino.")

def nivel10(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("nivel10")
    update.message.reply_text("Sent nivel10 command to Arduino.")

def nivel30(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("nivel30")
    update.message.reply_text("Sent nivel30 command to Arduino.")

def nivel60(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("nivel60")
    update.message.reply_text("Sent nivel60 command to Arduino.")

def nivel99(update: Update, context: CallbackContext) -> None:
    send_command_to_arduino("nivel99")
    update.message.reply_text("Sent nivel99 command to Arduino.")

def main():
    # Telegram bot token – replace with your bot's token
    TOKEN = '7730578439:AAHMcMsn6uOFxHZzYb1dxCDllRwg8Dd3XMw'
    
    updater = Updater(TOKEN, use_context=True)
    dp = updater.dispatcher

    # Add command handlers for each command
    dp.add_handler(CommandHandler("vib", vib))
    dp.add_handler(CommandHandler("son", son))
    dp.add_handler(CommandHandler("nivel1", nivel1))
    dp.add_handler(CommandHandler("nivel10", nivel10))
    dp.add_handler(CommandHandler("nivel30", nivel30))
    dp.add_handler(CommandHandler("nivel60", nivel60))
    dp.add_handler(CommandHandler("nivel99", nivel99))

    # Start the bot
    updater.start_polling()
    updater.idle()

if __name__ == '__main__':
    main()

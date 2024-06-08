#!/usr/bin/env python3

import time
import board
import busio
import digitalio
import adafruit_rfm9x
import mysql.connector
import logging
import pytz
from datetime import datetime

# Configure logging
logging.basicConfig(
    filename='read_lora.log',
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt='%Y-%m-%d %H:%M:%S'
)

# Distance from creek to ultrasonic sensor in centimeters (for Device 1)
creek_to_sensor_distance = 600

# Setup the connection to the RFM95W
CS = digitalio.DigitalInOut(board.CE1)
RESET = digitalio.DigitalInOut(board.D25)
spi = busio.SPI(board.SCK, MOSI=board.MOSI, MISO=board.MISO)
rfm9x = adafruit_rfm9x.RFM9x(spi, CS, RESET, 915.0)

# Increase LoRa power and spread
# rfm9x.tx_power = 20
# rfm9x.spreading_factor = 12  

logging.info("Starting up...")

def read_lora():
    packet = rfm9x.receive()
    if packet is not None:
        try:
            packet_text = packet.decode('ascii')
            signal_strength = rfm9x.last_rssi  # Get the signal strength of the received packet
            logging.info(f"Received: {packet_text}, Signal strength: {signal_strength} dBm")
            print(f"Received: {packet_text}, Signal strength: {signal_strength} dBm")
            return packet_text
        except UnicodeDecodeError:
            logging.warning(f"Received packet could not be decoded as ASCII: {packet}, Signal strength: {rfm9x.last_rssi} dBm")
    return None


def parse_data(data, prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour):
    # Determine the type of data based on the number of values
    values = data.split(',')
    if len(values) == 3:  # Data format for Device 1
        return parse_device_1_data(values), prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour
    elif len(values) == 7:  # Data format for Device 2
        return parse_device_rainfall_sensor(values, prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour)
    else:
        logging.error("Unexpected data format: %s", data)
        return None, prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour

def parse_device_1_data(values):
    # Assuming data is in the format "identifier,distance,battery_voltage"
    true_water_level = creek_to_sensor_distance - float(values[1])  # Subtracting the received distance from 6 meters
    parsed_values = {
        'table': 'water_level',
        'data': {
            'water_level': true_water_level,
            'battery_voltage': float(values[2])
        }
    }
    logging.info("Parsed Device 1 values: %s", str(parsed_values))
    return parsed_values

def parse_device_rainfall_sensor(values, prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour):
    current_daily_rainfall = float(values[4])
    current_sensor_on_time = float(values[2])
    
    if current_sensor_on_time < prev_sensor_on_time:
        # Daily reset detected
        five_minute_rainfall = current_daily_rainfall
    else:
        five_minute_rainfall = current_daily_rainfall - prev_daily_rainfall

    current_time_nz = datetime.now(pytz.timezone('Pacific/Auckland'))
    current_hour = current_time_nz.hour
    if current_hour != prev_hour:
        # Hourly reset detected
        hourly_rainfall = five_minute_rainfall
    else:
        hourly_rainfall = prev_hourly_rainfall + five_minute_rainfall

    parsed_values = {
        'table': 'rainfall_local',
        'data': {
            'sensor_on_time': current_sensor_on_time,
            'hourly_rainfall': hourly_rainfall,
            'daily_rainfall': current_daily_rainfall,
            'five_minute_rainfall': max(five_minute_rainfall, 0),  # Ensure no negative values
            'raw_bucket_tips': int(values[5]),
            'battery_voltage': float(values[6])
        }
    }
    logging.info("Parsed Device 2 values: %s", str(parsed_values))
    return parsed_values, current_daily_rainfall, current_sensor_on_time, hourly_rainfall, current_hour

def insert_data(parsed_values):
    table = parsed_values['table']
    values = parsed_values['data']

    # Convert current time to NZST before insertion
    nz_timezone = pytz.timezone('Pacific/Auckland')
    current_time_nz = datetime.now(nz_timezone)

    cnx = mysql.connector.connect(user='admin', password='what15ThisEven4?',
                                  host='172.17.0.3',
                                  database='creek_data')
    cursor = cnx.cursor()

    if table == 'water_level':
        add_data = ("INSERT INTO water_level "
                    "(timestamp, level, battery_voltage) "
                    "VALUES (%(timestamp)s, %(water_level)s, %(battery_voltage)s)")
        cursor.execute(add_data, {'timestamp': current_time_nz, 'water_level': values['water_level'], 'battery_voltage': values['battery_voltage']})
    elif table == 'rainfall_local':
        add_data = ("INSERT INTO rainfall_local "
                    "(timestamp, sensor_on_time, hourly_rainfall, daily_rainfall, five_minute_rainfall, raw_bucket_tips, battery_voltage) "
                    "VALUES (%(timestamp)s, %(sensor_on_time)s, %(hourly_rainfall)s, %(daily_rainfall)s, %(five_minute_rainfall)s, %(raw_bucket_tips)s, %(battery_voltage)s)")
        cursor.execute(add_data, {'timestamp': current_time_nz, **values})
    else:
        logging.error("Unexpected table name: %s", table)
        return

    cnx.commit()
    cursor.close()
    cnx.close()
    logging.info("Added to database")

# Initialize previous values
prev_daily_rainfall = 0.0
prev_sensor_on_time = 0.0
prev_hourly_rainfall = 0.0
prev_hour = datetime.now(pytz.timezone('Pacific/Auckland')).hour

while True:
    logging.info("Listening for transmission..")
    data = read_lora()
    if data is not None:
        logging.info("Transmission received")
        try:
            parsed_values, prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour = parse_data(data, prev_daily_rainfall, prev_sensor_on_time, prev_hourly_rainfall, prev_hour)
            if parsed_values:
                insert_data(parsed_values)
        except (ValueError, IndexError) as e:
            logging.error(f"Data parsing error: {e}")
    time.sleep(10)  # Adjust the polling interval as needed

import threading
import queue
import numpy as np
import json
import cv2
import base64
from time import sleep
import requests
from dateutil import parser
import datetime
from astropy.io import fits
from auto_stretch.stretch import Stretch
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
from time import sleep

# Ignore non Light frames

def getBayer(thing):
    name = "COLOR_BAYER_{}2RGB".format(thing)
    if not hasattr(cv2, name):
        raise RuntimeError
    return getattr(cv2, name)

def uploadFile(info):
    user = "pweber"
    key = "f6add7e0b953a30e9f8f7caf2a4553bb"
    baseUrl = "https://app.lightbucket.co"
    apiUrl = baseUrl + "/api/image_capture_complete"

    targetWidth = 300

    f = fits.open(info["filename"])[0]
    header = f.header
    if not "RA" in header or not "DEC" in header:
        # No target info, return
        return
    img = f.data
    mean = np.mean(img)
    if "BAYERPAT" in header:
        bayer = getBayer(header["BAYERPAT"])
        img = cv2.cvtColor(img, bayer)

    img = Stretch().stretch(img) * 255
    img = img.astype(np.uint8)
    img = cv2.resize(img, (targetWidth, int(img.shape[0] * targetWidth / img.shape[1])), interpolation=cv2.INTER_LANCZOS4)

    encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 70]
    is_success, buffer = cv2.imencode(".jpg", img, (cv2.IMWRITE_JPEG_QUALITY, 70))
    img64 = base64.b64encode(buffer)

    gain = None
    if "GAIN" in header:
        gain = header["GAIN"]
    elif "ISOSPEED" in header:
        gain = header["ISOSPEED"]

    targetData = {}
    if "OBJECT" in header and header["OBJECT"] != "":
        targetData["name"] = header["OBJECT"]
    if "RA" in header:
        targetData["ra"] = header["RA"]
    if "DEC" in header:
        targetData["dec"] = header["DEC"]
    if "CROTA1" in header:
        targetData["rotation"] = header["CROTA1"]

    equipmentData = {}
    if "INSTRUME" in header and header["INSTRUME"] != "":
        equipmentData["camera_name"] = header["INSTRUME"]
    if "TELESCOP" in header and header["TELESCOP"] != "":
        equipmentData["telescope_name"] = header["TELESCOP"]
    if "FOCALLEN" in header and header["FOCALLEN"] != 0:
        equipmentData["focal_length"] = header["FOCALLEN"]
        if "APTDIA" in header and header["APTDIA"] != 0:
            equipmentData["focal_ratio"] = header["FOCALLEN"] / header["APTDIA"]
    if "PIXSIZE1" in header and header["PIXSIZE1"] != 0:
        equipmentData["pixel_size"] = header["PIXSIZE1"]
    if "SCALE" in header and header["SCALE"] != 0:
        equipmentData["pixel_scale"] = header["SCALE"]

    statisticsData = {
                'hfr' : info["hfr"] if info["hfr"] != -1 else "NaN",
                'stars' : info["starCount"],
                'mean' : mean,
                'median' : info["median"],
            }

    imageData = {}
    imageData["statistics"] = statisticsData
    imageData["thumbnail"] = img64.decode("ascii")
    if "FILTER" in header and header["FILTER"] != "":
        imageData["filter_name"] = header["FILTER"]
    imageData["duration"] = header["EXPTIME"]
    if gain is not None:
        imageData["gain"] = gain
    if "OFFSET" in header:
        imageData["offset"] = header["OFFSET"]
    imageData["binning"] = "{}x{}".format(header["XBINNING"], header["YBINNING"])
    if "DATE-OBS" in header:
        imageData["captured_at"] = str(parser.parse(header["DATE-OBS"]))
    else:
        imageData["captured_at"] = str(datetime.datetime.now())


    data = {
            'target': targetData,
            'equipment': equipmentData,
            'image': imageData
            }
    payload = json.dumps(data)

    authString = str.encode("{}:{}".format(user, key))
    authString64 = base64.b64encode(authString).decode("ascii")

    res = requests.post(apiUrl, headers = {
            "Authorization" : "Basic " + authString64,
            "Content-Type" : "application/json"
        },
        data = str.encode(payload)
        )
    if res.status_code != 200:
        raise RuntimeError("Got code {} instead of 200: {}".format(res.status_code, res.text))

q = queue.Queue()

def worker():
    while True:
        try:
            item = q.get()
            print("Worker: Uploading file {}".format(item["filename"]))
            uploadFile(item)
        except Exception as e:
            print("Worker: Error processing file {}: {}".format(item["filename"], str(e)))
        q.task_done()

def signalHandler(info):
    if info["filename"] == "/tmp/image.fits":
        return
    if info["type"] != 0:
        return
    q.put(info)

threading.Thread(target=worker, daemon=True).start()

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
obj = bus.get_object(
        "org.kde.kstars",
        "/KStars/Ekos/Capture"
        )
obj.connect_to_signal("captureComplete", signalHandler)
GLib.MainLoop().run()

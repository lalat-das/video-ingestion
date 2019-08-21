
# Copyright (c) 2019 Intel Corporation.

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


import threading
import queue
import time
import datetime
import json
import os
import signal
import logging
import argparse
from distutils.util import strtobool
from ingestor import Ingestor
from libs.base_filter import load_filter
from libs.log import configure_logging, LOG_LEVELS
from libs.ConfigManager import ConfigManager
from publisher import Publisher


class VideoIngestion:

    def __init__(self, dev_mode, config_client):
        """Get the frames from camera or video, filters and add the results
        to the messagebus

        :param dev_mode: indicates whether it's dev or prod mode
        :type dev_mode: bool
        :param config_client: distributed store config client
        :type config_client: config client object
        """

        self.log = logging.getLogger(__name__)
        self.profiling = bool(strtobool(os.environ['PROFILING']))
        self.dev_mode = dev_mode
        self.app_name = os.environ["AppName"]
        self.config_client = config_client
        self._read_ingestor_filter_config()
        self.config_client.RegisterDirWatch("/{0}/".format(self.app_name),
                                            self._on_change_config_callback)

    def _print_config(self):
        self.log.info('ingestor_config: {}'.format(self.ingestor_config))
        if self.filter_name:
            self.log.info('filter name: {} filter config: {}'.format(
                self.filter_name, self.filter_config))

    def _read_ingestor_filter_config(self):
        CONFIG_KEY_PATH = "/config"
        self.config = self.config_client.GetConfig("/{0}{1}".format(
            self.app_name, CONFIG_KEY_PATH))

        self.config = json.loads(self.config)
        self.queue_size = 10  # default queue size for `no filter` config
        self.ingestor_config = self.config["ingestor"]
        self.filter_config = self.config.get("filter", None)
        if self.filter_config:
            self.filter_name = self.filter_config.get("name", None)
        else:
            self.filter_name = None

    def start(self):
        """Start Video Ingestion.
        """
        log_msg = "======={} {}======="
        self.log.info(log_msg.format("Starting", self.app_name))

        self._print_config()

        filter_input_queue = queue.Queue(
            maxsize=self.queue_size)

        if self.filter_name:
            queue_size = self.filter_config["queue_size"]
            filter_input_queue = queue.Queue(
                maxsize=queue_size)
            filter_output_queue = queue.Queue(
                maxsize=queue_size)
        else:
            filter_output_queue = filter_input_queue  # for `no filter` config

        self.publisher = Publisher(filter_output_queue,
                                   self.config_client, self.dev_mode)
        self.publisher.start()

        if self.filter_name:
            self.filter = load_filter(self.filter_config["name"],
                                      self.filter_config,
                                      filter_input_queue,
                                      filter_output_queue)
            self.filter.start()

        self.ingestor = Ingestor(self.ingestor_config, filter_input_queue)
        self.ingestor.start()
        self.log.info(log_msg.format("Started", self.app_name))

    def stop(self):
        """ Stop the Video Ingestion.
        """
        log_msg = "======={} {}======="
        self.log.info(log_msg.format("Stopping", self.app_name))
        self.ingestor.stop()
        if hasattr(self, "filter"):
            self.log.info("filter_name: {}".format(self.filter_name))
            self.filter.stop()
        self.publisher.stop()
        self.log.info(log_msg.format("Stopped", self.app_name))

    def _on_change_config_callback(self, key, value):
        """Callback method to be called by etcd

        :param key: Etcd key
        :type key: str
        :param value: Etcd value
        :type value: str
        """
        self.log.info("{}:{}".format(key, value))
        try:
            self._read_ingestor_filter_config()
            self.stop()
            self.start()
        except Exception as ex:
            self.log.exception(ex)


def parse_args():
    """Parse command line arguments

    :return: Object of Argument Parser
    :rtype: object
    """
    parser = argparse.ArgumentParser()
    parser.add_argument('--log-dir', dest='log_dir', default='logs',
                        help='Directory to for log files')

    return parser.parse_args()


def main():
    """Main method
    """
    # Parse command line arguments
    args = parse_args()

    currentDateTime = str(datetime.datetime.now())
    listDateTime = currentDateTime.split(" ")
    currentDateTime = "_".join(listDateTime)
    logFileName = 'videoingestion_' + currentDateTime + '.log'

    # Creating log directory if it does not exist
    if not os.path.exists(args.log_dir):
        os.mkdir(args.log_dir)

    dev_mode = bool(strtobool(os.environ["DEV_MODE"]))
    conf = {
        "certFile": "",
        "keyFile": "",
        "trustFile": ""
    }
    if not dev_mode:
        conf = {
            "certFile": "/run/secrets/etcd_VideoIngestion_cert",
            "keyFile": "/run/secrets/etcd_VideoIngestion_key",
            "trustFile": "/run/secrets/ca_etcd"
        }
    cfg_mgr = ConfigManager()
    config_client = cfg_mgr.get_config_client("etcd", conf)

    log = configure_logging(os.environ['PY_LOG_LEVEL'].upper(),
                            logFileName, args.log_dir,
                            __name__)

    vi = VideoIngestion(dev_mode, config_client)

    def handle_signal(signum, frame):
        log.info('Video Ingestion program killed...')
        vi.stop()

    signal.signal(signal.SIGTERM, handle_signal)

    try:
        vi.start()
    except Exception as ex:
        log.exception('Exception: {}'.format(ex))
        vi.stop()


if __name__ == '__main__':
    main()
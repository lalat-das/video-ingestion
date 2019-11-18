# `VideoIngestion Module`

This module ingests video frames from a video source like video file or
basler/RTSP/USB camera using gstreamer pipeline and publishes the
`(metadata, frame)` tuple to messagebus.

The high level logical flow of VideoIngestion pipeline is as below:
1. VideoIngestion main program reads the ingestor and filter configuration
2. After reading the config, it starts the messagebus publisher thread,
   single/multiple filter threads per filter configuration and ingestor thread
   based on ingestor configuration. It exits whenever an exception occurs during
   this startup sequence.
3. Ingestor thread reads from the ingestor configuration and adds
   data to ingestor queue
4. Based on the filter configuration, single or multiple filter
   threads consume ingestor queue and passes only the key frames with its
   metadata to publisher queue
5. Publisher thread reads from the publisher queue and publishes it
   over the message bus

## `Configuration`

All the VideoIngestion module configuration are added into etcd (distributed
key-value data store) under `AppName` as mentioned in the
environment section of this app's service definition in docker-compose.

If `AppName` is `VideoIngestion`, then the app's config would look like as below
 for `/VideoIngestion/config` key with `ingestor` configs in Etcd:
```
{
    "ingestor": {
        "type": "opencv",
        "pipeline": "./test_videos/pcb_d2000.avi",
        "loop_video": "true",
        "queue_size": 10,
        "poll_interval": 0.2
    }
}
```

> **NOTE**: The above `ingestor` config correspond to PCB demo
> usecase

For more details on Etcd and MessageBus endpoint configuration, visit [Etcd_and_MsgBus_Endpoint_Configuration](../Etcd_and_MsgBus_Endpoint_Configuration.md).


### `Ingestor config`

The following are the type of ingestors supported:

1. opencv
2. gstreamer

>**Note**: If running on non-gfx systems or older systems which doesn't have hardware media decoders it is recommended to use opencv ingestor

>**Note**: If GVA elements need to be used then it is recommended to use gstreamer ingestor


#### `OpenCV Ingestor config`

1. **Video file** (No Gstreamer pipeline involved before reading from OpenCV read API)
    ```
    {
        "type": "opencv",
        "pipeline": "./test_videos/pcb_d2000.avi",
        "poll_interval": 0.2
        "loop_video": "true"
    }
    ```
    >**NOTE**: Change the "pipeline" to classification sample
            `./test_videos/classification_vid.avi` for classification sample
            use case with `Dummy filter`.

2. **Basler camera**
   ```
    {
        "pipeline": "pylonsrc imageformat=yuv422 exposureGigE=3250 interpacketdelay=6000 ! videoconvert ! appsink",
    }

    Refer [Basler Camera](####Basler-Camera) section for more details on basler camera.

3. **RTSP cvlc based camera simulation**
   ```
    {
        "type": "opencv",
        "pipeline": "rtsp://localhost:8554/",
    }
    ```
    Refer [Start cvlc based RTSP stream](####Start-cvlc-based-RTSP-stream) section to start a cvlc based RTSP stream.

4. **RTSP camera**
   ```
    {
        "type": "opencv",
        "pipeline": "rtsp://admin:intel123@<RTSP CAMERA IP>:554",
    }

    Refer [RTSP Camera](####RTSP-Camera) section for more details on RTSP camera.

* If working behind a proxy, RTSP camera IP need to be updated to RTSP_CAMERA_IP in GlobalEnv in the etcd config.

5. **USB camera**
   ```
    {
        "type": "opencv",
        "pipeline": "/dev/video0",
    }

    Refer [USB camera](####USB-Camera) section for more details on USB camera.

    In case resizing needs to be enabled with opencv ingestor use the `Resize Udf`. The below udf config exmaple can be refered to use Resize Udf:
    ```
    "udfs": [{
            "name": "resize",
            "type": "native",
            "width": 600,
            "height":600
            }],
    ```

-------

#### `GStreamer Ingestor config`

The following are the configurations which can be used with the gstreamer ingestor:

1. **Video file**
    ```
    {
        "type": "gstreamer",
        "pipeline": "multifilesrc loop=TRUE location=./test_videos/pcb_d2000.avi ! h264parse ! decodebin ! video/x-raw,format=BGRx ! appsink",
    }
    ```
    >**NOTE**: Change the `location` parameter in "pipeline" to classification sample
                    `./test_videos/classification_vid.avi` for classification sample
                    use case with `Dummy filter`

2. **Basler camera**
   ```
    {
        "type": "gstreamer",
        "pipeline": "pylonsrc imageformat=yuv422 exposureGigE=3250 interpacketdelay=6000 ! videoconvert ! appsink",
    }
    Refer [Basler Camera](####Basler-Camera) section for more details on Basler Camera.

3. **RTSP cvlc based camera simulation**
    {
        "type": "gstreamer",
        "pipeline": "rtspsrc location=\"rtsp://localhost:8554/\" latency=100 ! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc format=bgrx ! video/x-raw,format=BGRx ! appsink",
    }
    Refer [RTSP cvcl based camera simulation](####RTSP-cvlc-based-camera-simulation) section for more details.

4. **RTSP camera**
    {
        "type": "gstreamer",
        "pipeline": "rtspsrc location=\"rtsp://admin:intel123@<RTSP CAMERA IP>:554/\" latency=100 ! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc format=bgrx ! video/x-raw,format=BGRx ! appsink",
    }

    * If working behind a proxy, RTSP camera IP need to be updated to RTSP_CAMERA_IP in [../docker_setup/.env](../docker_setup/.env) in the etcd config.
    Refer [RTSP Camera](####RTSP-Camera) section for more details on RTSP camera.

5. **USB camera**
    {
        "type": "gstreamer",
        "pipeline": "v4l2src ! decodebin ! videoconvert ! appsink",
    }
    Refer [USB Camera](####USB-Camera) section for more details on USB camera.

--------

**GVA Plugins**:

   **Note**: GVA plugins needs to be used only with `gstreamer` ingestor type.

   In case GVA plugin needs to be used the below configurations can be use:

   * When working with a USB camera :

            {
                "type": "gstreamer",
                "pipeline": "v4l2src ! decodebin ! videoconvert ! gvadetect model=models/face-detection-adas-0001.xml ! gvaclassify  model=models/emotions-recognition-retail-0003.xml model-proc=models/emotions-recognition-retail-0003.json ! gvaclassify  model=models/age-gender-recognition-retail-0013.xml model-proc=models/age-gender-recognition-retail-0013.json ! appsink"
            }


* When working with a RTSP camera :

        {
            "type": "gstreamer",
            "pipeline": "rtspsrc location=\"rtsp://admin:intel123@<RTSP CAMERA IP>:554/\" latency=100 ! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc format=bgrx ! video/x-raw,format=BGRx ! gvadetect model=models/face-detection-adas-0001.xml ! gvaclassify  model=models/emotions-recognition-retail-0003.xml model-proc=models/emotions-recognition-retail-0003.json ! gvaclassify  model=models/age-gender-recognition-retail-0013.xml model-proc=models/age-gender-recognition-retail-0013.json ! appsink"
        }


* When working with a Video file :

    To run the Safety Gear Detection Sample using GVA plugins the below pipeline:

        {
            "type": "gstreamer",
            "pipeline": "multifilesrc loop=TRUE location=./test_videos/Safety_Full_Hat_and_Vest.mp4 ! decodebin ! video/x-raw,format=BGRx ! gvadetect model=models/frozen_inference_graph.xml ! gvaclassify model=models/frozen_inference_graph.xml ! appsink"
        }

    **NOTE** : In case one needs to use CPU/GPU/HDDL device with GVA elements it can be set using the `device` property of `gvadetect` and `gvaclassify` elements.

    Example pipeline to run the Safety Gear Detection Sample using GVA plugins on HHDL device:

        {
            "type": "gstreamer",
            "pipeline": "multifilesrc loop=TRUE location=./test_videos/Safety_Full_Hat_and_Vest.mp4 ! decodebin ! video/x-raw,format=BGRx ! gvadetect device=HDDL model=models/frozen_inference_graph.xml ! gvaclassify device=HDDL model=models/frozen_inference_graph.xml ! appsink"
        }

    **Note** HDDL device needs to be configured on the system in order to use.
    **Note** Looping of videos is not happening when `.mp4` videos are used in the gstreamer pipeline with `multifilesrc`    plugin.

    Similarly the system needs to support gfx in order to set   `device=GPU`.

    In case Safety Gear Detection Sample needs to be used with a physical camera replace the `multifilesrc` source plugin with `v4l2src` or `rtspsrc` and
    provide the necessary configs.

    >**Note** : The labels are not getting generated when Safety Gear Detection Sample is used with GVA plugins.

   -------

   In case more information is needed regarding different camera configurations refer the below section for the respective cameras:

   #### Basler camera

    * In case multiple Basler cameras are connected use serial parameter to
        specify the camera to be used in the gstreamer pipeline in the video
        config file for camera mode. If multiple cameras are connected and the
        `serial` parameter is not specified then the source plugin by default
        connects to camera with device_index=0.

        **Eg**: `pipeline` value to connect to basler camera with
        serial number `22573664`:
        `"pipeline":"pylonsrc serial=22573664 imageformat=yuv422 exposureGigE=3250 interpacketdelay=1500 ! videoconvert ! appsink"`

    * In case you want to enable resizing with basler camera use the `vaapipostproc` element and specify the `height` and `width` parameter in the          gstreamer pipeline.

            **Eg**: Example pipeline to enable resizing with basler camera
            `"pipeline":"pylonsrc serial=22573664 imageformat=yuv422 exposureGigE=3250 interpacketdelay=1500 ! vaapipostproc height=600 width=600 ! videoconvert ! appsink"`

    * In case frame read is failing when multiple basler cameras are used, use
        the `interpacketdelay` property to increase the delay between the
        transmission of each packet for the selected stream channel.
        Depending on the number of cameras, use an appropriate delay can be set.

        **Eg**: `pipeline` value to increase the interpacket delay to 3000(default
        value for interpacket delay is 1500):

        `"pipeline":"pylonsrc imageformat=yuv422 exposureGigE=3250 interpacketdelay=3000 ! videoconvert ! appsink"`

    * To work with monochrome Basler camera, please change the
        image format to `mono8` in the Pipeline.

        **Eg**:`pipeline` value to connect to monochrome basler camera with serial
        number 22773747 :

        `"pipeline":"pylonsrc serial=22773747 imageformat=mono8   exposureGigE=3250 interpacketdelay=1500 ! videoconvert ! appsink"`

    * To work with USB Basler camera, please change the
        exposure parameter to `exposureUsb` in the Pipeline.

        `"pipeline":"pylonsrc serial=22773747 imageformat=mono8 exposureUsb=3250 interpacketdelay=1500 ! videoconvert ! appsink"`

    -------

    -------

    #### Start cvlc based RTSP stream

    * Install VLC if not installed already: `sudo apt install vlc`
    * In order to use the RTSP stream from cvlc, the RTSP server
        must be started using VLC with the following command:
        `cvlc -vvv file://<absolute_path_to_video_file> --sout '#gather:rtp{sdp=rtsp://localhost:8554/}' --loop --sout-keep`

    --------

   #### RTSP cvlc based camera simulation

    * In case you want to enable resizing with RTSP cvlc based camera use the `vaapipostproc` element and specifiy the `height` and `width` parameter in the          gstreamer pipeline.

        **Eg**: Example pipeline to enable resizing with RTSP camera
        `"pipeline": "rtspsrc location=\"rtsp://localhost:8554/\" latency=100 ! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc format=bgrx height=600 width=600 ! video/x-raw,format=BGRx ! appsink"`

   ------

    #### RTSP camera

    * In case you want to enable resizing with RTSP camera use the `vaapipostproc` element and specifiy the `height` and `width` parameter in the          gstreamer pipeline.

        **Eg**: Example pipeline to enable resizing with RTSP camera
        `"pipeline": "rtspsrc location=\"rtsp://admin:intel123@<RTSP CAMERA IP>:554/\" latency=100  ! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc format=bgrx height=600 width=600 ! video/x-raw,format=BGRx ! appsink"`

    * If working behind a proxy, RTSP camera IP need to be updated to RTSP_CAMERA_IP in GlobalEnv in the etcd config.
    * For working both with simulated RTSP server via cvlc or direct streaming
        from RTSP camera, we can use the below Gstreamer MediaSDK parsers and
        decoders based on the input stream type
        **Eg**: parsers and decoders:
        * h264parse !  vaapih264dec
        * h265parse ! vaapih265dec

        >**NOTE**: If running on non-gfx systems or older systems where we don't have hardware media decoders, the above parsers and decoders may not work. In those cases,
        one can use `opencv` ingestor and refer the below steps.

        * In case RTSP stream ingestion needs to be used on Xeon machine with no GPU then refer the following ingestor config,

        * If a physical RTSP camera is used:
            ```
            "pipeline": "rtsp://admin:intel123@<RTSP CAMERA IP>:554"
            ```
        * If a simulated RTSP stream needs to be used:

        * Run the following command to create a RTSP stream:
            ```
            docker run --rm -e RTSP_RESOLUTION='1920'x'1080' -e RTSP_FRAMERATE=25 -p 8554:8554 ullaakut/rtspatt
            ```
            If more options are required to generate a RTSP stream refer the following link:
            ```
            https://hub.docker.com/r/ullaakut/rtspatt/
            ```

        * Use the following config to read from the RTSP stream generated from the above command"
            ```
            "pipeline": "rtsp://localhost:8554/live.sdp"
            ```

        >**NOTE** : Some issues are observed with cvlc based camera simulation on a Xeon Machine with no GPU. In that case refer the above
        commands to generate a RTSP stream.

   ------

    #### USB camera

    * In case you want to enable resizing with USB camera use the `videoscale` element and specify the `height` and `width` parameter in the gstreamer pipeline.

        **Eg**: Example pipeline to enable resizing with USB camera
        `"pipeline":"v4l2src ! decodebin ! videoconvert ! videoscale ! video/x-raw, height=600, width=600 ! appsink"`

    * In case, multiple USB cameras are connected specify the
        camera using the `device` property in the configuration file.
        Eg:
        `"pipeline": "v4l2src device=/dev/video0 ! decodebin ! videoconvert ! appsink"`
   -------

#### `Detailed description on each of the keys used`

|  Key	        | Description 	                        | Possible Values  	                                            | Required/Optional	|
|---	        |---	                                |---	                                                        |---	            |
|  pipeline     |  Video source                         | Video file or gstreamer based pipeline 	                    | Required 	        |
|  poll_interval|  Determines fps read rate from camera | floating number  	                                            | Optional  	    |
|  loop_video	|  Would loop through the video file    | "true" or "false"	(By default, it's false)                    | Optional          |


### `Filter config`

The Filter (user defined function) is responsible for doing pre-processing of the
ingested video frames. It uses the filter configuration to do the selection of
key frames(frames of interest for further processing).

------

>**NOTE**: Please note if there is no `filter` key in the app's config, then
          it's as good as running the VI(VideoIngestion) pipeline without any
          filter thread/s. Functionally, it is equivalent to running with
          "Dummy filter" where the ingested frames are passed to classifier
          module as is without any pre-processing with added advantage of no
          filter threads.
------

**Sample configuration(forms the `filter` value in app's config) for filters used:**

1. **PCB filter**

   Works well with all PCB video file ingestors. To work with physical camera,
   proper setup is required with good lighting conditions. Proper training and
   tweaking filter and classifier logic may be required.

    Refer below example for adding pcb filter udf config:
    ```
        "udfs": [{
            "name": "pcb.pcb_filter",
            "type": "python",
            "training_mode": "false",
            "n_total_px": 300000,
            "n_left_px": 1000,
            "n_right_px": 1000
        }]
    ```


2. **Dummy filter**

   Works well with PCB or sample classfication video file ingestor. In general,
   works for any usecase where the ingested frames had be passed on to the
   classifier module as is without any pre-processing involved to select key
   frames.

   Refer below example for adding  filter udf in VideoIngestion config:
    ```
        "udfs": [{
            "name": "dummy",
            "type": "native"
        }]
    ```

#### `Detailed description on each of the keys used`

|  Key	        | Description 	                                                    | Possible Values  	                      | Required/Optional |
|---	        |---	                                                            |---	                                  |---	              |
|  name 	    |   File name of the filter	| "pcb_filter" or "Dummy filter"        | Required	                              |                   |
|  queue_size 	|   Determines the size of the input and output filter queue	    | any value that suits platform resources |   Required	      |
|  training_mode|  If "true", used to capture images for training and building model| "true" or "false" (default is false)    |   Optional        |


**Note**: The other keys used are specific to filter usecase

## `Installation`

* Follow [provision/README.md](../README#provision-eis.md) for EIS provisioning
  if not done already as part of EIS stack setup

* Run VideoIngestion

  Present working directory to try out below commands is: `[repo]/VideoIngestion`

    1. Build and Run VideoIngestion as container
        ```
        $ cd [repo]/docker_setup
        $ docker-compose up --build ia_video_ingestion
        ```
    2. Update EIS VideoIngestion config key value in `etcd` using UI
       like `EtcdKeeper` or programmatically. Please note that the dynamic
       update of the config only works for the "ingestor" key value without
       container restarts. If "filter" key value is changed, then the changes
       are picked by restarting the container.

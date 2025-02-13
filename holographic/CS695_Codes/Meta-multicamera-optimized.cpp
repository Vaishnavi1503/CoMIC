#include <librealsense2/rs.hpp>
#include <pcl/point_cloud.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/transforms.h>
#include <pcl/io/ply_io.h>
#include <pcl/filters/voxel_grid.h>
//#include "Meta/client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <signal.h>
#include <chrono>
#include <thread>

// create a type alias for the point cloud for RGB data.
typedef pcl::PointCloud<pcl::PointXYZ> pointCloudXYZ;
typedef pcl::PointCloud<pcl::PointXYZRGB> pointCloudXYZRGB;
// create a type alias for the type high_resolution_clock clockTime
typedef std::chrono::high_resolution_clock clockTime;
// create a type alias for the type std::chrono::time_point
typedef std::chrono::time_point<clockTime> timePoint;
// create a type alias for the type std::chrono::duration specialized with double, std::milli>
typedef std::chrono::duration<double, std::milli> timeMilli;

// defines the number of cameras in the system setup.
const int NUM_CAMERAS = 1;

// defining the other varabiles like ports fore client and the server.
const int CLIENT_PORT = 8000;
const int SERVER_PORT = 9000;
const int BUF_SIZE = 5000000;
const int STITCHED_BUF_SIZE = 32000000;
const float CONV_RATE = 1000.0;
const char PULL_XYZ = 'Y';
const char PULL_XYZRGB = 'Z';

// const std::string Meta_SERVER_ADDR("tcp://192.168.1.112:1883");
// const std::string TOPIC("orientation");
// const std::string Meta_CLIENT_ID("sewing_machine");
// const std::string IP_ADDRESS[8] = {"192.168.1.128", "192.168.1.142", "192.168.1.138", "192.168.1.114", 
//                                    "192.168.1.109", "192.168.1.113", "192.168.1.149", "192.168.1.131"};

// defining the IP address for the cameras.
const std::string IP_ADDRESS[8] =
    {   "localhost", "localhost", "192.168.1.138", "192.168.1.114",
        "192.168.1.109", "192.168.1.113", "192.168.1.149", "192.168.1.131"};
  
// defining the other varabiles
int loop_count = 1;
bool clean = true;
bool fast = false;
bool timer = false;
bool save = false;
bool visual = false;
int downsample = 1;
int framecount = 0;
int server_sockfd = 0;
int client_sockfd = 0;
int sockfd_array[NUM_CAMERAS];
short * stitched_buf;

// Declaring the 4X4 matrics which can be used for transformation.
Eigen::Matrix4f transform[NUM_CAMERAS];
// Declaring the threading for cameras.
std::thread Meta_thread[NUM_CAMERAS];
// declaring the point cloud Visualizer for displaying point clouds.
pcl::visualization::PCLVisualizer viewer("Pointcloud Stitching");


// This Function handles the signal.
void sigintHandler(int dummy) {
    
    for (int i = 0; i < NUM_CAMERAS; i++) {
        close(sockfd_array[i]);
    }
    close(server_sockfd);
    close(client_sockfd);
}

// Function to get Arguments from the terminal
void parseArgs(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "hftsvd:n")) != -1) {
        switch(c) {
            
            case 'n':
                clean = false;
                break;
            case 'f':
                fast = true;
                break;
            
            case 't':
                timer = true;
                break;
            
            case 's':
                save = true;
                break;
           
            case 'v':
                visual = true;
                break;
            
            case 'd':
                downsample = atoi(optarg);
                break;
            default:
            case 'h':
                std::cout << "\nMulticamera pointcloud stitching" << std::endl;
                std::cout << "Usage: Meta-multicamera-client [options]\n" << std::endl;
                std::cout << "Options:" << std::endl;
                std::cout << " -h (help)        Display command line options" << std::endl;
                std::cout << " -f (fast)        Increases the frame rate at the expense of color" << std::endl;
                std::cout << " -t (timer)       Displays the runtime of certain functions" << std::endl;
                std::cout << " -s (save)        Saves 20 frames in a .ply format" << std::endl;
                std::cout << " -v (visualize)   Visualizes the pointclouds using PCL visualizer" << std::endl;
                std::cout << " -d (downsample)  Downsamples the stitched pointcloud by the specified integer" << std::endl;
                exit(0);
        }
    }
}

// Create TCP socket with specific port and IP address for server.
int initSocket(int port, std::string ip_addr) {

    std::cout << "i0 port:" << port << "Ip" << ip_addr<< std::endl;

    int sockfd;
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_addr.c_str(), &serv_addr.sin_addr);

    std::cout << "i1" << std::endl;

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Couldn't create socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "i2" << std::endl;


    // Connect to camera server
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection failed at " << ip_addr << "." << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "i3" << std::endl;

    std::cout << "Connection made at " << sockfd << std::endl;
    return sockfd;
}

// Create TCP socket with specific port and IP address for unity client.
int initServerSocket() {
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(SERVER_PORT);

    if ((server_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        std::cerr << "\nSocket fd not received." << std::endl;
        exit(EXIT_FAILURE);
    }

    if (bind(server_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "\nBind failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (listen(server_sockfd, 3) < 0) {
        std::cerr << "\nListen failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "\nWaiting for VR Headset client..." << std::endl;

    if ((client_sockfd = accept(server_sockfd, NULL, NULL)) < 0) {
        std::cerr << "\nConnection failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Established connection with unity client_sock: " << client_sockfd << std::endl;
}

// Function which is used to send the pullback request to server.
void sendPullRequest(int sockfd, char pull_char) {
    if (send(sockfd, &pull_char, 1, 0) < 0) {
        std::cerr << "Pull request failure from sockfd: " << sockfd << std::endl;
        exit(EXIT_FAILURE);
    }
}

// Function to Read the data form the server 
void readNBytes(int sockfd, unsigned int n, void * buffer) {
    int total_bytes, bytes_read;
    total_bytes = 0;

    
    while (total_bytes < n) {
        if ((bytes_read = read(sockfd, buffer + total_bytes, n - total_bytes)) < 1) {
            std::cerr << "Receive failure" << std::endl;
            exit(EXIT_FAILURE);
        }

        total_bytes += bytes_read;
    }
}

// Function to convert the Buffer data which we got from server into pointcloud.
pointCloudXYZRGB::Ptr convertBufferToPointCloudXYZRGB(short * buffer, int size) {
    int count = 0;
    pointCloudXYZRGB::Ptr new_cloud(new pointCloudXYZRGB);

    new_cloud->width = size / downsample;
    new_cloud->height = 1;
    new_cloud->is_dense = false;
    new_cloud->points.resize(new_cloud->width);

    for (int i = 0; i < size; i++) {
        if (i % downsample == 0) {
            new_cloud->points[count].x = (float)buffer[i * 5 + 0] / CONV_RATE;
            new_cloud->points[count].y = (float)buffer[i * 5 + 1] / CONV_RATE;
            new_cloud->points[count].z = (float)buffer[i * 5 + 2] / CONV_RATE;
            new_cloud->points[count].r = (uint8_t)(buffer[i * 5 + 3] & 0xFF);
            new_cloud->points[count].g = (uint8_t)(buffer[i * 5 + 3] >> 8);
            new_cloud->points[count].b = (uint8_t)(buffer[i * 5 + 4] & 0xFF);
            count++;
        }
    }
    
    return new_cloud;
}

// Converting the point cloud to buffer to send through network.
int convertPointCloudXYZRGBToBuffer(pointCloudXYZRGB::Ptr cloud, short * buffer) {
    int size = 0;

    for (int i = 0; i < cloud->width; i++) {
        buffer[size * 5 + 0] = static_cast<short>(cloud->points[i].x * CONV_RATE);
        buffer[size * 5 + 1] = static_cast<short>(cloud->points[i].y * CONV_RATE);
        buffer[size * 5 + 2] = static_cast<short>(cloud->points[i].z * CONV_RATE);
        buffer[size * 5 + 3] = static_cast<short>(cloud->points[i].r) + static_cast<short>(cloud->points[i].g << 8);
        buffer[size * 5 + 4] = static_cast<short>(cloud->points[i].b);
      
        size++;
    }

    return size;
}

// Reads from the buffer and converts the data into a new XYZRGB pointcloud.
void updateCloudXYZRGB(int thread_num, int sockfd, pointCloudXYZRGB::Ptr cloud) {
    double update_total, convert_total;
    timePoint loop_start, loop_end, read_start, read_end_convert_start, convert_end;

    if (timer)
        read_start = std::chrono::high_resolution_clock::now();

    short * cloud_buf = (short *)malloc(sizeof(short) * BUF_SIZE);
    int size;

   // reading the data from the server.
    readNBytes(sockfd, sizeof(int), (void *)&size);
    readNBytes(sockfd, size, (void *)&cloud_buf[0]);
    
    // Sending the pullback request to server.
    sendPullRequest(sockfd, PULL_XYZRGB);

    if (timer)
        read_end_convert_start = std::chrono::high_resolution_clock::now();

    // converting the buffer which we got form the server into point cloud.
    *cloud = *convertBufferToPointCloudXYZRGB(&cloud_buf[0], size / sizeof(short) / 5);
    pcl::transformPointCloud(*cloud, *cloud, transform[thread_num]);

    free(cloud_buf);

    if (timer) {
        convert_end = std::chrono::high_resolution_clock::now();
        std::cout << "updateCloud " << thread_num << ": " << timeMilli(convert_end - read_end_convert_start).count() << " ms" << std::endl;
    }
}
// this function is to send the buffer data to VR client. 
void send_stitchedXYZRGB(pointCloudXYZRGB::Ptr stitched_cloud) {
    char pull_request[1] = {0};

    // Wait for pull request
    if (recv(client_sockfd, pull_request, 1, 0) < 0) {
        std::cout << "Client disconnected" << std::endl;
        exit(0);
    }
    if (pull_request[0] == 'Z') {         
        int size = convertPointCloudXYZRGBToBuffer(stitched_cloud, &stitched_buf[0] + sizeof(short));
        size = 5 * size * sizeof(short);
        memcpy(stitched_buf, &size, sizeof(int));
        
        write(client_sockfd, (char *)stitched_buf, size + sizeof(int));
    }
    else {                                      // Did not receive a correct pull request
        std::cerr << "Faulty pull request" << std::endl;
        exit(EXIT_FAILURE);
    }
}

// Function in which we are runing the stichting to combine frames from multiple cameras.
void runStitching() {
    double total;
    timePoint loop_start, loop_end, stitch_start, stitch_end_viewer_start;
    
    std::vector <pointCloudXYZRGB::Ptr, Eigen::aligned_allocator <pointCloudXYZRGB::Ptr>> cloud_ptr(NUM_CAMERAS);
    pointCloudXYZRGB::Ptr stitched_cloud(new pointCloudXYZRGB);
    pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> cloud_handler(stitched_cloud);


    std::cout << "-1" << std::endl;
    
    if (visual) {
        // setting the Background color
        viewer.setBackgroundColor(0.05, 0.05, 0.05, 0);
        // Add the point cloud to the visualization
        viewer.addPointCloud(stitched_cloud, cloud_handler, "cloud");
        // setting rendering properties to point cloud.
        viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 2, "cloud");
    }
    
    std::cout << "0" << std::endl;

    // sending the point cloud size as pull back to server.
    for (int i = 0; i < NUM_CAMERAS; i++) {
        cloud_ptr[i] = pointCloudXYZRGB::Ptr(new pointCloudXYZRGB);
        sendPullRequest(sockfd_array[i], PULL_XYZRGB);
    }

    std::cout << "1" << std::endl;

   
    while (1) {
        if (timer)
            loop_start = std::chrono::high_resolution_clock::now();

        if (clean) stitched_cloud->clear();

        if (timer)
            stitch_start = std::chrono::high_resolution_clock::now();
       
        
        for (int i = 0; i < NUM_CAMERAS; i++) {
            Meta_thread[i] = std::thread(updateCloudXYZRGB, i, sockfd_array[i], cloud_ptr[i]);
        }
        
        for (int i = 0; i < NUM_CAMERAS; i++) {
            Meta_thread[i].join();
            *stitched_cloud += *cloud_ptr[i];
        }

        if (timer)
            stitch_end_viewer_start = std::chrono::high_resolution_clock::now();

        // updating the point cloud.
        if (visual) {
            viewer.updatePointCloud(stitched_cloud, "cloud");
            viewer.spinOnce();

            if (viewer.wasStopped()) {
                exit(0);
            }
        }
        else {
            // sending the data to the VR client.
            send_stitchedXYZRGB(stitched_cloud);
        }

        if (timer) {
            double temp = timeMilli(stitch_end_viewer_start - stitch_start).count();
            total += temp;
            std::cout << "Stitch average: " << total / loop_count << " ms" << std::endl;
            loop_count++;
        }
        // saving the files in plfy format.
        if (save) {
            std::string filename("pointclouds/stitched_cloud_"  + std::to_string(framecount) + ".ply");
            pcl::io::savePLYFileBinary(filename, *stitched_cloud);
            std::cout << "Saved frame " << framecount << std::endl;
            framecount++;
            if (framecount == 20)
                save = false;
        }
    }
}

int main(int argc, char** argv) {

    parseArgs(argc, argv);

    stitched_buf = (short *)malloc(sizeof(short) * STITCHED_BUF_SIZE);

    /* Reminder: how transformation matrices work :
                 |-------> This column is the translation, which represents the location of the camera with respect to the origin
    | r00 r01 r02 x |  \
    | r10 r11 r12 y |   }-> Replace the 3x3 "r" matrix on the left with the rotation matrix
    | r20 r21 r22 z |  /
    |   0   0   0 1 |    -> We do not use this line (and it has to stay 0,0,0,1)
    */

transform[0] << -0.69888007, -0.32213748,  0.63858757, -2.22900000,
                -0.71520905,  0.32290986, -0.61984291,  2.91800000,
                -0.00653159, -0.88991947, -0.45607091,  0.36400000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[1] << -0.96127595,  0.09045863, -0.26031862,  0.31700000,
                 0.27558764,  0.31552831, -0.90801615,  2.83300000,
                 0.00000000, -0.94459469, -0.32823906,  0.38100000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[2] << -0.63305575,  0.28270490, -0.72063747,  2.80300000,
                 0.77409926,  0.22724638, -0.59087175,  2.05500000,
                -0.00328008, -0.93189968, -0.36270128,  0.42100000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[3] <<  0.17021299,  0.28598815, -0.94299433,  2.51000000,
                 0.98527137, -0.03349883,  0.16768470, -0.27300000,
                 0.01636663, -0.95764743, -0.28747787,  0.35900000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[4] <<  0.72625904,  0.26139935, -0.63578155,  1.90900000,
                 0.68735231, -0.26305364,  0.67701520, -2.81700000,
                 0.00972668, -0.92869433, -0.37071853,  0.37900000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[5] <<  0.98744750,  0.00686296,  0.15779838, -0.57400000,
                -0.14665062, -0.33120318,  0.93209337, -2.69700000,
                 0.05866025, -0.94353450, -0.32603930,  0.30900000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[6] <<  0.67295609,  0.40193638,  0.62094867, -2.97300000,
                -0.35777412, -0.55787451,  0.74884826, -0.41700000,
                 0.64740079, -0.72610136, -0.23162261,  0.43400000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

transform[7] <<  0.08929624, -0.21535297,  0.97244500, -2.95700000,
                -0.67610010, -0.73004840, -0.09958907, -0.33900000,
                 0.73137872, -0.64857723, -0.21079074,  0.33800000,
                 0.00000000,  0.00000000,  0.00000000,  1.00000000;

    sockfd_array[0] = initSocket(CLIENT_PORT, "localhost");
    
    if (!visual) initServerSocket();
    
    signal(SIGINT, sigintHandler);
    
    runStitching();

    close(sockfd_array[0]); 

    close(server_sockfd);
    close(client_sockfd);
    
    return 0;
}
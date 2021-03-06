#include "stdafx.h"
#include <stdio.h>

// UHD stuff
#include <uhd.h>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/subdev_spec.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>


// Parsing the INI File
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <string>
#include <complex>
#include <csignal>

// Moodycamel SPSC lock-less queue
#include "concurrentqueue.h"

// Global Vars
boost::property_tree::ptree pt;

// File storage stuff
std::string filepath;

// Usrp Global props
double sampleRate;
double gain;
int total_num_samps;
bool writeData;

// USRP 0 Params
std::string devaddr0;
std::string filename0;
double centerfrq0;
// USRP 1 Params
std::string devaddr1;
std::string filename1;
double centerfrq1;
// USRP 2 Params
std::string devaddr2;
std::string filename2;
double centerfrq2;

// Multi USRP sptr;
uhd::usrp::multi_usrp::sptr dev;

volatile static bool stop_signal_called = false;
void sig_int_handler(int a) { stop_signal_called = true; }

void createUSRPs(void)
{
	// Preping to create the multi USRP
	uhd::device_addr_t dev_addr;

	dev_addr["addr0"] = devaddr0;
	dev_addr["addr1"] = devaddr1;
	dev_addr["addr2"] = devaddr2;

	std::cout << dev_addr.to_pp_string() << std::endl;

	// Create the Multi USRP
	dev = uhd::usrp::multi_usrp::make(dev_addr);

	// Set all the subdevs
	uhd::usrp::subdev_spec_t subdev("A:0");

	dev->set_rx_subdev_spec(subdev);
	std::cout << boost::format("Using Device: %s") % dev->get_pp_string() << std::endl;

	// Make sure the clock source is external
	dev->set_clock_source("external");

	// Set the sample rate please note that I am scaling up the double
	// this means that the samplerate in the config file should be in Msps.
	dev->set_rx_rate(sampleRate*1e6);

	// Set the center freq
	uhd::tune_request_t tune_request0(centerfrq0*1e6);
	dev->set_rx_freq(tune_request0, 0);

	uhd::tune_request_t tune_request1(centerfrq1*1e6);
	dev->set_rx_freq(tune_request1, 1);

	uhd::tune_request_t tune_request2(centerfrq2*1e6);
	dev->set_rx_freq(tune_request2, 2);

	// Set the gains
	dev->set_rx_gain(gain);
}

void printUSRPInfo() {
	std::cout << boost::format("Actual RX Rate: %f Msps...") % (dev->get_rx_rate(2) / 1e6) << std::endl << std::endl;
	std::cout << boost::format("Actual RX Freq: %f MHz...") % (dev->get_rx_freq() / 1e6) << std::endl << std::endl;
	std::cout << boost::format("Actual RX Gain: %f dB...") % dev->get_rx_gain() << std::endl << std::endl;
	//std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % dev->get_rx_bandwidth() << std::endl << std::endl;

}

void write_to_file_thread(const std::string file, moodycamel::ConcurrentQueue<std::complex<short> > *q) 
{
	// Do not run this loop if there is no need to write data. 
	if (!writeData)
		return;

	std::ofstream outfile;
	outfile.open(file.c_str(), std::ofstream::binary);
	std::complex<short> data;

	if (outfile.is_open()) 
	{
		while (!stop_signal_called)
		{
			while (q->try_dequeue(data)) 
			{
				outfile.write((const char*)&data, sizeof(data));
				free(&data);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		outfile.close();
		printf("File %s written!\n", file.c_str());
	}
	else 
	{
		printf("File %s failed to open.\n", file.c_str());
	}
}

int UHD_SAFE_MAIN(int argc, char ** argv)
{
	bool result = uhd::set_thread_priority_safe();

	if (!result)
	{
		printf("Failed to make thread priority safe!\n");
	}


	writeData = false;
	if (argc < 2)
	{
		// Automatically terminate if no config file provided.
		printf("Not enough parameters...\n\t Ex: ./datarec cfg_file.ini\n");
		return -1;
	}

	// Open the provided file.
	printf("Attemping to open file: %s\n", argv[1]);
	boost::property_tree::ini_parser::read_ini("config.ini", pt);


	// Get the global params.
	filepath = pt.get<std::string>("Global.filepath");
	sampleRate = std::stod(pt.get<std::string>("Global.samplerate"));
	gain = std::stod(pt.get<std::string>("Global.gain"));
	total_num_samps = std::stoi(pt.get<std::string>("Global.total_num_samps"));

	printf("All files will be stored in directory %s.\nSample Rate is %0.2f MSps\nUSRP Gain is %fdB\n", filepath.c_str(), sampleRate, gain);


	// Get USRP 0 params
	devaddr0 = pt.get<std::string>("USRP0.deviceaddr");
	filename0 = pt.get<std::string>("USRP0.filename");
	centerfrq0 = std::stod(pt.get<std::string>("USRP0.centerfrq"));

	printf("Using USRP [%s] to record to %s%s with a center frq of %0.2f Mhz\n", devaddr0.c_str(), filepath.c_str(), filename0.c_str(), centerfrq0);

	// Get USRP 1 params
	devaddr1 = pt.get<std::string>("USRP1.deviceaddr");
	filename1 = pt.get<std::string>("USRP1.filename");
	centerfrq1 = std::stod(pt.get<std::string>("USRP1.centerfrq"));

	printf("Using USRP [%s] to record to %s%s with a center frq of %0.2f Mhz\n", devaddr1.c_str(), filepath.c_str(), filename1.c_str(), centerfrq1);


	// Get USRP 0 params
	devaddr2 = pt.get<std::string>("USRP2.deviceaddr");
	filename2 = pt.get<std::string>("USRP2.filename");
	centerfrq2 = std::stod(pt.get<std::string>("USRP2.centerfrq"));

	printf("Using USRP [%s] to record to %s%s with a center frq of %0.2f Mhz\n", devaddr2.c_str(), filepath.c_str(), filename2.c_str(), centerfrq2);

	printf("Creating USRPs\n");
	createUSRPs();
	printf("Created USRPs\n");

	printUSRPInfo();

	// Setup rx streamer
	// For external clock source, set time
	dev->set_time_unknown_pps(uhd::time_spec_t(0.0));
	boost::this_thread::sleep_for(boost::chrono::seconds(1)); // Wait for pps sync pulse

	std::string channel_list("0,1,2");
	std::vector<std::string> channel_strings;
	std::vector<size_t> channel_nums;
	boost::split(channel_strings, channel_list, boost::is_any_of("\"',"));
	for (size_t ch = 0; ch < channel_strings.size(); ch++) 
	{
		size_t chan = boost::lexical_cast<int>(channel_strings[ch]);
		if (chan >= dev->get_rx_num_channels()) 
		{
			printf("Invalid channel(s) specified.\n");
			return -1;
		}
		else
		{
			channel_nums.push_back(boost::lexical_cast<int>(channel_strings[ch]));
		}
	}

	// Create a receive streamer
	uhd::stream_args_t stream_args("sc16"); //complex floats
	std::vector<size_t> channel = { 0,1,2 };
	stream_args.channels = channel;
	uhd::rx_streamer::sptr rx_stream = dev->get_rx_stream(stream_args);

	// Setup streaming
	printf("\nBegin streaming %d samples...\n", total_num_samps);
	uhd::stream_cmd_t stream_cmd((total_num_samps == 0) ?
		uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS :
		uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE
	);
	double seconds_in_future = 1.5;
	stream_cmd.num_samps = total_num_samps;
	stream_cmd.stream_now = false;
	stream_cmd.time_spec = uhd::time_spec_t(seconds_in_future);
	rx_stream->issue_stream_cmd(stream_cmd);
	if (total_num_samps == 0) {
		std::signal(SIGINT, &sig_int_handler);
		printf("Press Ctrl + C to stop streaming...\n");
	}

	// Buffer to store the pointers to the data.
	std::vector<std::complex<short> *> buff_ptrs;
	const size_t samps_per_buff = rx_stream->get_max_num_samps();

	if (!writeData)
	{
		// Allocate buffers to receive with samples (one buffer per channel)
		std::cout << "samps_per_buff: " << samps_per_buff << std::endl;
		std::cout << "rx_num_channels: " << dev->get_rx_num_channels() << std::endl;
		std::vector<std::vector<std::complex<short> > > buffs(
			dev->get_rx_num_channels(), std::vector<std::complex<short> >(samps_per_buff)
		);

		// Create a vector of pointers to point to each of the channel buffers
		for (size_t i = 0; i < buffs.size(); i++) {
			buff_ptrs.push_back(&buffs[i].front());
		}
	}
	
	moodycamel::ConcurrentQueue<std::complex<short> > q0;
	moodycamel::ConcurrentQueue<std::complex<short> > q1;
	moodycamel::ConcurrentQueue<std::complex<short> > q2;


	// Consumer threads
	boost::thread c0(write_to_file_thread, filepath + filename0, &q0);
	boost::thread c1(write_to_file_thread, filepath + filename1, &q1);
	boost::thread c2(write_to_file_thread, filepath + filename2, &q2);
	
	

	// Streaming loop
	size_t num_acc_samps = 0; //number of accumulated samples
	bool overflow_message = true;
	uhd::rx_metadata_t md;

	double timeout = seconds_in_future + 0.1; //timeout (delay before receive + padding)
	size_t num_overflow = 0;

	printf("\nBeginning streaming:\n");
	while (!stop_signal_called) {

		if (writeData)
		{
			std::complex<short> *data1 = new std::complex<short>[samps_per_buff];
			std::complex<short> *data2 = new std::complex<short>[samps_per_buff];
			std::complex<short> *data3 = new std::complex<short>[samps_per_buff];

			buff_ptrs.push_back(data1);
			buff_ptrs.push_back(data2);
			buff_ptrs.push_back(data3);
		}


		size_t num_rx_samps = rx_stream->recv(
			buff_ptrs, samps_per_buff, md, timeout
		);

		timeout = 0.1;

		//std::cout << "num_rx_samps: " << num_rx_samps << std::endl;
		// Handle the error code
		if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
			printf("Timeout while streaming\n");
			break;
		}
		if (md.out_of_sequence) {
			printf("Out of sequence\n");
		}
		if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
			num_overflow++;
			if (overflow_message) {
				overflow_message = false;
				std::cerr << boost::format(
					"Got an overflow indication. Please consider the following:\n"
					"  Your write medium must sustain a rate of %fMB/s.\n"
					"  Dropped samples will not be written to the file.\n"
					"  This message will not appear again.\n"
				) % (dev->get_rx_rate() * sizeof(std::complex<float>) / 1e6);
			}
			continue;
		}
		if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
			printf("Receiver error: %s\n", md.strerror().c_str());
		}

		std::cout << boost::format(
			"Received packet: %u samples, %u full secs, %f frac secs"
		) % num_rx_samps % md.time_spec.get_full_secs() % md.time_spec.get_frac_secs() << std::endl;

		// Enque data
		if (writeData)
		{
			q0.try_enqueue(*buff_ptrs.at(0));
			q1.try_enqueue(*buff_ptrs.at(1));
			q2.try_enqueue(*buff_ptrs.at(2));

			// We enqued everything, so clear it out.
			buff_ptrs.clear();
		}


		num_acc_samps += num_rx_samps;
	}
	std::cout << "Stopping..." << std::endl;
	rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);


	printf("Number of samples received: ");
	std::cout << num_acc_samps << std::endl;

	std::cout << "Number of overflow: " << num_overflow << std::endl;

	if (num_acc_samps < total_num_samps && !stop_signal_called) {
		printf("Received timeout before all samples received...\n");
	}


	// Wait for the other threads to finish up and join back with the main one.
	// Allows for the other threads to close the files if necessary.
	c0.join();
	c1.join();
	c2.join();

	// Finished
	printf("\nDone!\n");

	return EXIT_SUCCESS;
}
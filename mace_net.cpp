
#include "mace_net.hpp"
#include "common.hpp"
#include <sstream>
using namespace mace;
using namespace std;
using namespace cv;


  
namespace 
{
  // bbox overlap
  inline float overlap(const BBox_v2 &a, const BBox_v2 &b) 
  {
    if (a.xmin > b.xmax || a.xmax < b.xmin ||
        a.ymin > b.ymax || a.ymax < b.ymin) 
    {
      return 0.f;
    }
    float overlap_w = std::min(a.xmax, b.xmax) - std::max(a.xmin, b.xmin);
    float overlap_h = std::min(a.ymax, b.ymax) - std::max(a.ymin, b.ymin);
    return overlap_w * overlap_h;
  }
  // NMS
  void NmsSortedBboxes(const std::vector<BBox_v2> &bboxes,
                      const float nms_threshold,
                      const int top_k,
                      std::vector<BBox_v2> *sorted_boxes) 
  {
    const int n = std::min(top_k, static_cast<int>(bboxes.size()));
    std::vector<int> picked;

    std::vector<float> areas(n);
  #pragma omp parallel for schedule(runtime)
    for (int i = 0; i < n; ++i) 
    {
      const BBox_v2 &r = bboxes[i];
      float width = std::max(0.f, r.xmax - r.xmin);
      float height = std::max(0.f, r.ymax - r.ymin);
      areas[i] = width * height;
    }

    for (int i = 0; i < n; ++i) 
    {
      const BBox_v2 &a = bboxes[i];
      int keep = 1;
      for (size_t j = 0; j < picked.size(); ++j) 
      {
        const BBox_v2 &b = bboxes[picked[j]];

        float inter_area = overlap(a, b);
        float union_area = areas[i] + areas[picked[j]] - inter_area;
        //MACE_CHECK(union_area > 0, "union_area should be greater than 0");
        if(union_area <= 0)
        {
          std::cout<<"union_area should be greater than 0"<<std::endl;
          exit(-1);
        }
        if (inter_area / union_area > nms_threshold) 
        {
          keep = 0;
          break;
        }
      }
      if (keep) 
      {
        picked.push_back(i);
        sorted_boxes->push_back(bboxes[i]);
      }
    }
  }
  // compare cls score
  inline bool cmp(const BBox_v2 &a, const BBox_v2 &b) 
  {
    return a.confidence > b.confidence;
  }
}  


bool ReadBinaryFile(std::vector<unsigned char> *data,
                           const std::string &filename) 
{
  std::ifstream ifs(filename, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) 
  {
    return false;
  }
  ifs.seekg(0, ifs.end);
  size_t length = ifs.tellg();
  ifs.seekg(0, ifs.beg);

  data->reserve(length);
  data->insert(data->begin(), std::istreambuf_iterator<char>(ifs),
               std::istreambuf_iterator<char>());
  if (ifs.fail()) 
  {
    return false;
  }
  ifs.close();

  return true;
}

DeviceType ParseDeviceType(const std::string &device_str) 
{
  if (device_str.compare("CPU") == 0) 
  {
    return DeviceType::CPU;
  } else if (device_str.compare("GPU") == 0) 
  {
    return DeviceType::GPU;
  } else if (device_str.compare("HEXAGON") == 0) 
  {
    return DeviceType::HEXAGON;
  } else 
  {
    return DeviceType::CPU;
  }
}

int activation_function_softmax(const float* src, float* dst, int length)
{
	const float alpha = *std::max_element(src, src + length);
	float denominator{ 0 };
	for (int i = 0; i < length; ++i) 
  {
		dst[i] = std::exp(src[i] - alpha);
		denominator += dst[i];
	}
 
	for (int i = 0; i < length; ++i) {
		dst[i] /= denominator;
	}
 
	return 0;
}

int draw_frame(const cv::Mat &img, const vector<Track> &tracks, const vector<BBox_v2> &boxes, State_Dict &state, 
               const cv::Mat &mask, const cv::Scalar &color, const int &vis_track_len, cv::Mat &dst)
{
  cv::add(img, mask, dst);

  // add statistics
  stringstream ss_on;
  ss_on << "O N: ";
  ss_on << state.on;
  cv::putText(dst, ss_on.str(), cv::Point(0, 60), cv::FONT_HERSHEY_COMPLEX, 0.5, color);

  stringstream ss_off;
  ss_off << "OFF: ";
  ss_off << state.off;
  cv::putText(dst, ss_off.str(), cv::Point(0, 80), cv::FONT_HERSHEY_COMPLEX, 0.5, color);

  // draw boxes and print score
  for(int num_index=0; num_index < boxes.size(); num_index++)
  {	
    cv::Rect r(boxes[num_index].xmin,//left
              boxes[num_index].ymin,//top
              boxes[num_index].xmax-boxes[num_index].xmin,//width
              boxes[num_index].ymax-boxes[num_index].ymin);//height
    stringstream ss_person;
    ss_person << "person | ";
    ss_person << boxes[num_index].confidence;
    cv::rectangle(dst,r, cv::Scalar(0, 255, 0), 2);
    cv::putText(dst, ss_person.str(), cv::Point(boxes[num_index].xmin, boxes[num_index].ymin-2), cv::FONT_HERSHEY_COMPLEX, 0.5, color);
  }

  for(int i =0; i<tracks.size();i++)
  {
    int length = tracks[i].POINTS.size();
    if(length==1)
    {
      cv::circle(dst, cv::Point(tracks[i].POINTS[0][0], tracks[i].POINTS[0][1]), 3, tracks[i].color(), -1);
      continue;
    }
    else if(length==2)
    {
      float a = tracks[i].POINTS[length-1][0];
      float b = tracks[i].POINTS[length-1][1];
      float c = tracks[i].POINTS[length-2][0];
      float d = tracks[i].POINTS[length-2][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-2][0];
      b = tracks[i].POINTS[length-2][1];
      c = tracks[i].POINTS[length-1][0];
      d = tracks[i].POINTS[length-1][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
    }
    else if(length==3)
    {
      float a = tracks[i].POINTS[length-1][0];
      float b = tracks[i].POINTS[length-1][1];
      float c = tracks[i].POINTS[length-2][0];
      float d = tracks[i].POINTS[length-2][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-2][0];
      b = tracks[i].POINTS[length-2][1];
      c = tracks[i].POINTS[length-3][0];
      d = tracks[i].POINTS[length-3][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-3][0];
      b = tracks[i].POINTS[length-3][1];
      c = tracks[i].POINTS[length-1][0];
      d = tracks[i].POINTS[length-1][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
    }
    else if(length==4)
    {
      float a = tracks[i].POINTS[length-1][0];
      float b = tracks[i].POINTS[length-1][1];
      float c = tracks[i].POINTS[length-2][0];
      float d = tracks[i].POINTS[length-2][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-2][0];
      b = tracks[i].POINTS[length-2][1];
      c = tracks[i].POINTS[length-3][0];
      d = tracks[i].POINTS[length-3][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-3][0];
      b = tracks[i].POINTS[length-3][1];
      c = tracks[i].POINTS[length-4][0];
      d = tracks[i].POINTS[length-4][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-4][0];
      b = tracks[i].POINTS[length-4][1];
      c = tracks[i].POINTS[length-1][0];
      d = tracks[i].POINTS[length-1][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
    }
    else
    {
      float a = tracks[i].POINTS[length-1][0];
      float b = tracks[i].POINTS[length-1][1];
      float c = tracks[i].POINTS[length-2][0];
      float d = tracks[i].POINTS[length-2][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-2][0];
      b = tracks[i].POINTS[length-2][1];
      c = tracks[i].POINTS[length-3][0];
      d = tracks[i].POINTS[length-3][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-3][0];
      b = tracks[i].POINTS[length-3][1];
      c = tracks[i].POINTS[length-4][0];
      d = tracks[i].POINTS[length-4][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
      a = tracks[i].POINTS[length-4][0];
      b = tracks[i].POINTS[length-4][1];
      c = tracks[i].POINTS[length-5][0];
      d = tracks[i].POINTS[length-5][1];
      cv::line(dst,cv::Point(a,b),cv::Point(c,d),tracks[i].color(), 2);
      cv::circle(dst,cv::Point(a,b), 3, tracks[i].color(), -1);
    }
  }
	return 0;
}

bool in_neighbor(const vector<float> &p1, const vector<float> &p2, const vector<float> &thr)
{
  if(fabs(p1[0]-p2[0])<thr[0] && fabs(p1[1]-p2[1])<thr[1])
  {
    return true;
  }
    return false;
}


void track_update_with_boxes(vector<Track> &tracks, 
                             const vector<BBox_v2> &boxes,
                             const vector<int> &RoI, 
                             vector<Track> &final_tracks,
                             const float &box_track_thr,
                             vector<cv::Scalar> &colors)
{
  std::vector<bool> skip_bb (boxes.size(), false);
  for(auto &track : tracks)
  {
    if(track.finished())
    {
      continue;
    }

    vector<float> last_point = track.lastPoint();

    for(int j=0;j<boxes.size();j++)
    {
      if(skip_bb[j])
      {
        continue;
      }

      vector<float> thr = {boxes[j].width*box_track_thr, boxes[j].height*box_track_thr};
      vector<float> ctr = {boxes[j].xctr, boxes[j].yctr};

      if (!in_neighbor(last_point, ctr, thr))
      {
        continue;
      }
      
      track.updateLastPointWithBox(ctr);
      skip_bb[j]=true;
      break;
    }

    if( track.lastPoint()[0]<RoI[0] || 
        track.lastPoint()[0]>RoI[2] || 
        track.lastPoint()[1]<RoI[1] || 
        track.lastPoint()[1]>RoI[3])
    {
      track.finalize();
      final_tracks.push_back(track);
    }
  }

  
  for(auto i = tracks.begin();i!=tracks.end();)
  { 
    if(i->finished())
    {
      i = tracks.erase(i);
    }
    else
    {
      i++;
    }
  }

  vector<int> tracks_colors;
  for(auto track : tracks)
  {
    tracks_colors.push_back(track.color_type());
  }
  

  vector<int> color_index;

  for(int i=0;i<colors.size();i++)
  {
    bool is_found = false;
    for(int j=0; j<tracks_colors.size();j++)
    {
      if(i==tracks_colors[j])
      {
        is_found = true;
      }
    }
    if(!is_found)
    {
      color_index.push_back(i);
    }
  }

  for(int i=0;i<boxes.size();i++)
  {
    if(boxes[i].xctr < RoI[0] || 
      boxes[i].xctr > RoI[2] || 
      boxes[i].yctr < RoI[1] || 
      boxes[i].yctr > RoI[3])
    {
      continue;
    }

    if(skip_bb[i])
    {
      continue;
    }
    Track single_track = Track({boxes[i].xctr, boxes[i].yctr}, colors[color_index[i]], color_index[i]);
    tracks.push_back(single_track);
  }
}


MACENet::MACENet(const string&model_name)
// :BaseNet(model_name),_input_shape_vec(1),_output_shape_vec(12) // !!! need to change if number of output changes !!!
:BaseNet(model_name),_output_shape_vec(12) // !!! need to change if number of output changes !!!
{  
  _anchors_all = _generate_anchors.generate_anchors_busdet(_basesize_ratio_range, _step, _input_size,
                                                           _anchor_strides, _anchor_ratios, _feat_size);
}

bool MACENet::LoadModel(const string& model_file, const string& weight_file)
{
  //LOG(INFO)<<"Beginning load model";
  string FLAGS_device = "CPU";
  std::cout<<"Device Mode *******" << FLAGS_device <<"*******"<<std::endl;

  DeviceType device_type = ParseDeviceType(FLAGS_device);
  MaceEngineConfig _config(device_type);
 
  //set CPU config
  // int FLAGS_omp_num_threads = -1;
  // int FLAGS_cpu_affinity_policy = 1;
  int FLAGS_omp_num_threads = -1;
  int FLAGS_cpu_affinity_policy = 3;

  MaceStatus status;
  status = _config.SetCPUThreadPolicy(
      FLAGS_omp_num_threads,
      static_cast<CPUAffinityPolicy >(FLAGS_cpu_affinity_policy));
  if (status != MaceStatus::MACE_SUCCESS) 
  {
    std::cerr << "Set openmp or cpu affinity failed." << std::endl;
  }


  //set GPU config
  if(FLAGS_device == "GPU")
  {

    std::cout << "Beigin to set gpu config.\n";

    int FLAGS_gpu_perf_hint = 3;
    int FLAGS_gpu_priority_hint = 3;
    _config.SetGPUHints(
        static_cast<GPUPerfHint>(FLAGS_gpu_perf_hint),
        static_cast<GPUPriorityHint>(FLAGS_gpu_priority_hint));
    std::cout << "set succss.\n";
  }


  string FLAGS_model_file = model_file;
  string FLAGS_model_data_file = weight_file;
  // std::vector<unsigned char> model_graph_data;
  if (!ReadBinaryFile(&model_graph_data, FLAGS_model_file)) 
  {
    std::cerr << "Failed to read file: " << FLAGS_model_file << std::endl;
  }
  // std::vector<unsigned char> model_weights_data;
  if (!ReadBinaryFile(&model_weights_data, FLAGS_model_data_file)) 
  {
    std::cerr << "Failed to read file: " << FLAGS_model_data_file << std::endl;
  }

  std::cout << "CreateMaceEngineFromProto start.\n";
  for(size_t n=0; n<_input_names.size(); n++)
  {	
	  std::cout << "_input_names:"<<_input_names[n]<<std::endl;
  }
  for(size_t n=0; n<_output_names.size(); n++)
  {	
  	  std::cout <<"_output_names:"<<_output_names[n]<<std::endl;
  }
  // _create_engine_status = CreateMaceEngineFromProto(model_graph_data,                              
  //                             FLAGS_model_data_file,                              
  //                             _input_names,
  //                             _output_names,
  //                             _config,
  //                             &_engine);
  _create_engine_status = CreateMaceEngineFromProto(&model_graph_data[0],
                              model_graph_data.size(),                              
                              &model_weights_data[0],
                              model_weights_data.size(),                              
                              _input_names,
                              _output_names,
                              _config,
                              &_engine);
  if (_create_engine_status != MaceStatus::MACE_SUCCESS) 
  {
    std::cerr << "CreateMaceEngineFromProto failed." << std::endl;
    return false;
  }
  std::cout << "CreateMaceEngineFromProto end.\n";
    // LOG(INFO)<<"Ending load model";
  return true;
}

bool MACENet::SetThreadNum(const int num_threads)
{

    //omp_set_num_threads(num_threads);

    return true;
}

// 提取网络最后的输出
void MACENet::prepareOutputData()
{
  // _output_names = {"577","590","639","652"};
  _output_shape_vec[0].push_back(1);// batch, every time comes in one image
  _output_shape_vec[0].push_back(8);// number of features of this output entity
  _output_shape_vec[0].push_back(19);// feature size of this output entity
  _output_shape_vec[0].push_back(19);

  _output_shape_vec[1].push_back(1);
  _output_shape_vec[1].push_back(12);
  _output_shape_vec[1].push_back(10);
  _output_shape_vec[1].push_back(10);

  _output_shape_vec[2].push_back(1);
  _output_shape_vec[2].push_back(12);
  _output_shape_vec[2].push_back(5);
  _output_shape_vec[2].push_back(5);

  _output_shape_vec[3].push_back(1);
  _output_shape_vec[3].push_back(12);
  _output_shape_vec[3].push_back(3);
  _output_shape_vec[3].push_back(3);

  _output_shape_vec[4].push_back(1);
  _output_shape_vec[4].push_back(8);
  _output_shape_vec[4].push_back(2);
  _output_shape_vec[4].push_back(2);

  _output_shape_vec[5].push_back(1);
  _output_shape_vec[5].push_back(8);
  _output_shape_vec[5].push_back(1);
  _output_shape_vec[5].push_back(1);

  _output_shape_vec[6].push_back(1);
  _output_shape_vec[6].push_back(16);
  _output_shape_vec[6].push_back(19);
  _output_shape_vec[6].push_back(19);

  _output_shape_vec[7].push_back(1);
  _output_shape_vec[7].push_back(24);
  _output_shape_vec[7].push_back(10);
  _output_shape_vec[7].push_back(10);

  _output_shape_vec[8].push_back(1);
  _output_shape_vec[8].push_back(24);
  _output_shape_vec[8].push_back(5);
  _output_shape_vec[8].push_back(5);

  _output_shape_vec[9].push_back(1);
  _output_shape_vec[9].push_back(24);
  _output_shape_vec[9].push_back(3);
  _output_shape_vec[9].push_back(3);

  _output_shape_vec[10].push_back(1);
  _output_shape_vec[10].push_back(16);
  _output_shape_vec[10].push_back(2);
  _output_shape_vec[10].push_back(2);

  _output_shape_vec[11].push_back(1);
  _output_shape_vec[11].push_back(16);
  _output_shape_vec[11].push_back(1);
  _output_shape_vec[11].push_back(1);

}

// 提取结果做预测
bool MACENet::Predict(const cv::Mat&img,std::vector<BBox_v2>* bbox_rects,const string&output_name,int output_idx)
{
   cv::Mat input=Preprocess(img);
   return Run(input,bbox_rects,output_name,output_idx);
}

// 实际预测前向过程
bool MACENet::Run(const cv::Mat&img, std::vector<BBox_v2>* bbox_rects, const string&output_name,int output_idx)
{
  string FLAGS_input_file = "";
	
  const size_t input_count = _input_names.size();
  const size_t output_count = _output_names.size();
  
	
  for (size_t i = 0; i < output_count; ++i) 
  {
    int64_t output_size =
        std::accumulate(_output_shape_vec[i].begin(), _output_shape_vec[i].end(), 1,
                        std::multiplies<int64_t>());
    auto buffer_out = std::shared_ptr<float>(new float[output_size],
                                             std::default_delete<float[]>());
    _outputs[_output_names[i]] = MaceTensor(_output_shape_vec[i], buffer_out);
  }
   
  for (size_t i = 0; i < input_count; ++i) 
  {
    int64_t input_size =std::accumulate(_input_shape_vec[i].begin(), _input_shape_vec[i].end(), 1,
                        std::multiplies<int64_t>());
    auto buffer_in = std::shared_ptr<float>(new float[input_size],
                                            std::default_delete<float[]>()); 
    // vector<float> std={58.395, 57.12, 57.375}; 
    vector<float> std={0.229, 0.224, 0.225};
    for (int h=0; h < _input_shape[2]; ++h)
    {
      for (int w=0; w < _input_shape[3]; ++w)
      {
        for (int c=0; c < _input_shape[1]; ++c)
        {
          int index_tmp = h*_input_shape[3]*_input_shape[1] + w*_input_shape[1] + c;
          buffer_in.get()[index_tmp] = img.at<cv::Vec3f>(h,w)[c]/std[c];
        }
      }
    }
    _inputs[_input_names[i]] = MaceTensor(_input_shape_vec[i], buffer_in,mace::DataFormat::NHWC);
  }

  // CPUTimer modelrun_starttime;
  // TIMER_START(modelrun_starttime);
  _engine->Run(_inputs, &_outputs);
  // int modelrun_endtime = TIMER_STOP(modelrun_starttime);
	// std::cout<<"model run consumed: " << modelrun_endtime << " ms." << std::endl;
  
  // CPUTimer t1;
  // TIMER_START(t1); 
  // 存储网络输出的结果
  _clc_map[0] = _outputs["603"].data().get();
  _loc_map[0] = _outputs["607"].data().get();
  _clc_map[1] = _outputs["611"].data().get();
  _loc_map[1] = _outputs["615"].data().get();
  _clc_map[2] = _outputs["619"].data().get();
  _loc_map[2] = _outputs["623"].data().get();
  _clc_map[3] = _outputs["627"].data().get();
  _loc_map[3] = _outputs["631"].data().get();
  _clc_map[4] = _outputs["635"].data().get();
  _loc_map[4] = _outputs["639"].data().get();
  _clc_map[5] = _outputs["643"].data().get();
  _loc_map[5] = _outputs["647"].data().get();

  // 得到检测框	
  std::vector<BBox_v2> class_bbox_rects;
  
  for (int stage_i= 0; stage_i < _anchors_all.size(); stage_i++)
  {
    for(int bbox_i= 0; bbox_i < _anchors_all[stage_i].size(); bbox_i++)
    {  
      int loc_index = bbox_i * 4;
      int clc_index = bbox_i * 2;
      const float *loc = _loc_map[stage_i] + loc_index;
      const float *clc = _clc_map[stage_i] + clc_index;
      
      float *clc_softmax = new float[2];
      activation_function_softmax(clc,clc_softmax,2);
      
      if(clc_softmax[1] >= _threshold)
      {
        anchor_box regress1;
        regress1.x1 = loc[0]*_img_std[0];//dx
        regress1.y1 = loc[1]*_img_std[1];//dy
        regress1.x2 = loc[2]*_img_std[2];//dw
        regress1.y2 = loc[3]*_img_std[3];//dh         
        anchor_box proposal = _generate_anchors.bbox_pred(_anchors_all[stage_i][bbox_i], regress1); 
        _generate_anchors.clip_boxes(proposal, _max_shape[0], _max_shape[1]); //越界处理
        proposal.x1 = proposal.x1/_scale[1];
        proposal.y1 = proposal.y1/_scale[0];
        proposal.x2 = proposal.x2/_scale[1];
        proposal.y2 = proposal.y2/_scale[0];
        
        BBox_v2 bbox = {proposal.x1,
                        proposal.y1,
                        proposal.x2,
                        proposal.y2, 
                        1, 
                        clc_softmax[1],
                        (proposal.x1+proposal.x2)/2,
                        (proposal.y1+proposal.y2)/2,
                        proposal.x2-proposal.x1,
                        proposal.y2-proposal.y1
                        };
        class_bbox_rects.push_back(bbox);
      }
      delete[] clc_softmax;  
    }
  }
  
  // apply nms
  vector<BBox_v2> sorted_boxes;
  NmsSortedBboxes(class_bbox_rects,
				  _nms_threshold,
				  std::min(_top_k,
						 static_cast<int>(class_bbox_rects.size())),
				  &sorted_boxes);
  // gather
  bbox_rects->insert(bbox_rects->end(), sorted_boxes.begin(),
				     sorted_boxes.end());
  std::sort(bbox_rects->begin(), bbox_rects->end(), cmp);
  
  // output
  int num_detected = _keep_top_k < static_cast<int>(bbox_rects->size()) ?
                     _keep_top_k : static_cast<int>(bbox_rects->size());

  bbox_rects->resize(num_detected);
  
  // int average_time = TIMER_STOP(t1);
  // std::cout<<"Post-process Time consumed: " << average_time << " ms." << std::endl;

  // std::cout<<"detec "<<  bbox_rects->size()<<std::endl;
  return true;
}

int main()
{
	//1.read an image
	//2.construct a network
		// CPUTimer t1;
	// TIMER_START(t1);
  vector<cv::Scalar> colors = {
    {224, 41, 88},
    {149, 113, 84},
    {219, 110, 192},
    {48, 146, 236},
    {136, 116, 232},
    {244, 223, 77},
    {179, 153, 192},
    {197, 124, 158},
    {225, 250, 168},
    {158, 233, 252},
    {69, 138, 250},
    {90, 197, 198},
    {112, 159, 140},
    {224, 175, 121},
    {43, 229, 241},
    {203, 225, 180},
    {114, 157, 138},
    {107, 52, 110},
    {140, 75, 240},
    {20, 83, 192},
    {151, 24, 89},
    {240, 96, 26},
    {183, 86, 253},
    {157, 58, 41},
    {57, 59, 178},
    {47, 224, 174},
    {70, 185, 219},
    {73, 58, 204},
    {66, 143, 82},
    {30, 157, 234},
    {76, 46, 37},
    {146, 40, 27},
    {92, 88, 122},
    {62, 224, 58},
    {212, 184, 99},
    {184, 224, 89},
    {176, 142, 244},
    {78, 251, 71},
    {108, 204, 218},
    {134, 117, 63}
  };

	MACENet mymace("busdet");
	std::cout<<"construct success"<<std::endl;

	//3.load converted model
	mymace.LoadModel("./model_busdet/busdet.pb","./model_busdet/busdet.data");
  mymace.prepareOutputData();
	std::cout<<"load model success"<<std::endl;
	
  // A whole lot of parameters:
  cv::Size Win_Size = cv::Size(31, 31);
  int MaxLevel = 3;
  cv::TermCriteria Creteria = cv::TermCriteria(3, 10, 0.03);

  float Box_Track_Thr = 0.25;
  float Flow_Neighbor_X = 30;
  float Flow_Neighbor_Y = 30;
  int middle_thr = 335;
  int vis_track_len = 10;
  vector<int> RoI = {64, 18 ,524, 349};
  cv::Scalar color = {0,255,0};
  
  State_Dict state = {0, 0};
//==============================================================================
  cv::VideoCapture cap;
  cap.open("/home/zhuminchen/mace_busdet/vid1.mp4");
  if(!cap.isOpened())
  {  
    return 1;
  }

  vector<Track> Tracks;
  vector<Track> Final_Tracks;
  
  // int name_index = 0;
  int counter=0;
  cv::Mat prev_gray;
  cv::Mat img_pre;
  // cap.set(cv::CAP_PROP_POS_FRAMES,counter); 
  cap.read(img_pre);
  // img_pre = cv::imread("./vid1/vid_0.jpg");
  cv::cvtColor(img_pre, prev_gray, cv::COLOR_BGR2GRAY);

  // cv::Mat mask = cv::Mat::zeros(cap.get(cv::CAP_PROP_FRAME_HEIGHT), cap.get(cv::CAP_PROP_FRAME_WIDTH), CV_8UC(3));
  cv::Mat mask = cv::Mat::zeros(368, 640, CV_8UC(3));
  cv::rectangle(mask, cv::Point(RoI[0],RoI[1]), cv::Point(RoI[2],RoI[3]), cv::Scalar(0,255,0), 4,-1);
  cv::line(mask, cv::Point(middle_thr, RoI[1]), cv::Point(middle_thr, RoI[3]), cv::Scalar(0,255,0), 4,-1);

  int cnt = 0;
  // cap.set(CV_CAP_PROP_POS_FRAMES,counter+1); 
  cv::Mat img;

  std::string img_root = "./vid1/vid_";
  
  while(cap.read(img))

  { 
    // name_index++;
    // img = cv::imread(img_root +std::to_string(counter)+ ".jpg");  
    std::vector<BBox_v2> bbox_rects; 

    mymace.Predict(img,&bbox_rects,"name");
    
    if(Tracks.size()==0)
    {
      if(bbox_rects.size()>0)
      {
        track_update_with_boxes(Tracks, bbox_rects, RoI, Final_Tracks, Box_Track_Thr, colors);
      }
      cv::Mat dst;
      int draw = draw_frame(img, Tracks, bbox_rects, state, mask, color, vis_track_len, dst);
      // cv::imwrite(std::string("./res/result_")+std::to_string(counter)+std::string(".jpg"), dst);
      cv::imshow("sparse optical flow",dst);
      cv::waitKey(1);
      counter++;
      std::cout<<counter<<std::endl;
      continue;
    }

    vector<cv::Point2f> prev;
    vector<cv::Point2f> next;
    for(auto track : Tracks)
    { 
      prev.push_back(cv::Point2f(track.lastPoint()[0], track.lastPoint()[1]));
    }

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    vector<unsigned char> status;
    vector<float> err;
    cv::calcOpticalFlowPyrLK(prev_gray, gray, prev, next, status, err, Win_Size, MaxLevel, Creteria);
    
    for(int i = 0;i<Tracks.size();i++)
    { 
      if(status[i])
      {
        if (in_neighbor(Tracks[i].lastPoint(),{next[i].x, next[i].y}, {Flow_Neighbor_X, Flow_Neighbor_Y}))
        {
          Tracks[i].addFlow({next[i].x, next[i].y});
        }
      }
    }
    gray.copyTo(prev_gray);

    track_update_with_boxes(Tracks, bbox_rects, RoI, Final_Tracks, Box_Track_Thr, colors);
    for(auto i = Final_Tracks.rbegin();i!=Final_Tracks.rend();)
    {
      float start_x = i->startPoint()[0];
      float end_x = i->lastPoint()[0];
      if(start_x<middle_thr && end_x > middle_thr)
      {
        state.off = state.off+1;
      }
      else if(start_x > middle_thr && end_x < middle_thr)
      {
        state.on = state.on+1;
      }
      else
      {
        ;
      }
      i++;
    }
    Final_Tracks.clear();

    cv::Mat dst;
    draw_frame(img, Tracks, bbox_rects, state, mask, color, vis_track_len, dst);
    // cv::imwrite(std::string("./res/result_")+std::to_string(counter)+std::string(".jpg"), dst);
    cv::imshow("sparse optical flow",dst);
    cv::waitKey(1);
    std::cout<<counter<<std::endl;
    counter++;
    cnt++;
    // std::cout<<cnt<<std::endl;
  }
	return 0;
  // cap.release();
}

//#endif

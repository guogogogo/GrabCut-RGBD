#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include <iostream>

using namespace std;
using namespace cv;

static void help()
{
    cout << "\nThis program demonstrates GrabCut segmentation -- select an object in a region\n"
            "and then grabcut will attempt to segment it out.\n"
            "Call:\n"
            "./grabcut <image_name>\n"
        "\nSelect a rectangular area around the object you want to segment\n" <<
        "\nHot keys: \n"
        "\tESC - quit the program\n"
        "\tr - restore the original image\n"
        "\tn - next iteration\n"
        "\n"
        "\tleft mouse button - set rectangle\n"
        "\n"
        "\tCTRL+left mouse button - set GC_BGD pixels\n"
        "\tSHIFT+left mouse button - set CG_FGD pixels\n"
        "\n"
        "\tCTRL+right mouse button - set GC_PR_BGD pixels\n"
        "\tSHIFT+right mouse button - set CG_PR_FGD pixels\n" << endl;
}

const Scalar RED = Scalar(0,0,255);
const Scalar PINK = Scalar(230,130,255);
const Scalar BLUE = Scalar(255,0,0);
const Scalar LIGHTBLUE = Scalar(255,255,160);
const Scalar GREEN = Scalar(0,255,0);

const int BGD_KEY = CV_EVENT_FLAG_CTRLKEY;  //Ctrl��
const int FGD_KEY = CV_EVENT_FLAG_SHIFTKEY; //Shift��

static void getBinMask( const Mat& comMask, Mat& binMask )
{
    if( comMask.empty() || comMask.type()!=CV_8UC1 )
        CV_Error( CV_StsBadArg, "comMask is empty or has incorrect type (not CV_8UC1)" );
    if( binMask.empty() || binMask.rows!=comMask.rows || binMask.cols!=comMask.cols )
        binMask.create( comMask.size(), CV_8UC1 );
    binMask = comMask & 1;  //�õ�mask�����λ,ʵ������ֻ����ȷ���Ļ����п��ܵ�ǰ���㵱��mask
}

class GCApplication
{
public:
    enum{ NOT_SET = 0, IN_PROCESS = 1, SET = 2 };
    static const int radius = 2;
    static const int thickness = -1;

    void reset();
    void setImageAndWinName( const Mat& _image, const string& _winName );
    void showImage() const;
    void mouseClick( int event, int x, int y, int flags, void* param );
    int nextIter();
    int getIterCount() const { return iterCount; }

	// �Լ��ӵĺ���
	void GCApplication::GetTrimap(Mat& _mask);
private:
    void setRectInMask();
    void setLblsInMask( int flags, Point p, bool isPr );

    const string* winName;
    const Mat* image;
	Mat mask;
    Mat bgdModel, fgdModel;

    uchar rectState, lblsState, prLblsState;
    bool isInitialized;

    Rect rect;
    vector<Point> fgdPxls, bgdPxls, prFgdPxls, prBgdPxls;
    int iterCount;
};

/*����ı�����ֵ*/
void GCApplication::reset()
{
    if( !mask.empty() )
        mask.setTo(Scalar::all(GC_BGD));
    bgdPxls.clear(); fgdPxls.clear();
    prBgdPxls.clear();  prFgdPxls.clear();

    isInitialized = false;
    rectState = NOT_SET;    //NOT_SET == 0
    lblsState = NOT_SET;
    prLblsState = NOT_SET;
    iterCount = 0;
}

/*����ĳ�Ա������ֵ����*/
void GCApplication::setImageAndWinName( const Mat& _image, const string& _winName  )
{
    if( _image.empty() || _winName.empty() )
        return;
    image = &_image;
    winName = &_winName;
    mask.create( image->size(), CV_8UC1);
    reset();
}

/*��ʾ4���㣬һ�����κ�ͼ�����ݣ���Ϊ����Ĳ���ܶ�ط���Ҫ�õ�������������Ե����ó���*/
void GCApplication::showImage() const
{
    if( image->empty() || winName->empty() )
        return;

    Mat res;
    Mat binMask;
    if( !isInitialized )
        image->copyTo( res );
    else
    {
        getBinMask( mask, binMask );
        image->copyTo( res, binMask );  //�������λ��0����1�����ƣ�ֻ������ǰ���йص�ͼ�񣬱���˵���ܵ�ǰ�������ܵı���
    }
    vector<Point>::const_iterator it;
    /*����4������ǽ�ѡ�е�4�����ò�ͬ����ɫ��ʾ����*/
    for( it = bgdPxls.begin(); it != bgdPxls.end(); ++it )  //���������Կ�����һ��ָ��
        circle( res, *it, radius, BLUE, thickness );
    for( it = fgdPxls.begin(); it != fgdPxls.end(); ++it )  //ȷ����ǰ���ú�ɫ��ʾ
        circle( res, *it, radius, RED, thickness );
    for( it = prBgdPxls.begin(); it != prBgdPxls.end(); ++it )
        circle( res, *it, radius, LIGHTBLUE, thickness );
    for( it = prFgdPxls.begin(); it != prFgdPxls.end(); ++it )
        circle( res, *it, radius, PINK, thickness );

    /*������*/
    if( rectState == IN_PROCESS || rectState == SET )
        rectangle( res, Point( rect.x, rect.y ), Point(rect.x + rect.width, rect.y + rect.height ), GREEN, 2);

    imshow( *winName, res );
	
}

/*�ò�����ɺ�maskͼ����rect�ڲ���3������ȫ��0*/
void GCApplication::setRectInMask()
{
    assert( !mask.empty() );
    mask.setTo( GC_BGD );   //GC_BGD == 0
    rect.x = max(0, rect.x);
    rect.y = max(0, rect.y);
    rect.width = min(rect.width, image->cols-rect.x);
    rect.height = min(rect.height, image->rows-rect.y);
    (mask(rect)).setTo( Scalar(GC_PR_FGD) );    //GC_PR_FGD == 3�������ڲ�,Ϊ���ܵ�ǰ����
}

void GCApplication::setLblsInMask( int flags, Point p, bool isPr )
{
    vector<Point> *bpxls, *fpxls;
    uchar bvalue, fvalue;
    if( !isPr ) //ȷ���ĵ�
    {
        bpxls = &bgdPxls;
        fpxls = &fgdPxls;
        bvalue = GC_BGD;    //0
        fvalue = GC_FGD;    //1
    }
    else    //���ʵ�
    {
        bpxls = &prBgdPxls;
        fpxls = &prFgdPxls;
        bvalue = GC_PR_BGD; //2
        fvalue = GC_PR_FGD; //3
    }
    if( flags & BGD_KEY )
    {
        bpxls->push_back(p);
        circle( mask, p, radius, bvalue, thickness );   //�õ㴦Ϊ2
    }
    if( flags & FGD_KEY )
    {
        fpxls->push_back(p);
        circle( mask, p, radius, fvalue, thickness );   //�õ㴦Ϊ3
    }
}

/*�����Ӧ����������flagsΪCV_EVENT_FLAG�����*/
void GCApplication::mouseClick( int event, int x, int y, int flags, void* )
{
    // TODO add bad args check
    switch( event )
    {
    case CV_EVENT_LBUTTONDOWN: // set rect or GC_BGD(GC_FGD) labels
        {
            bool isb = (flags & BGD_KEY) != 0,
                 isf = (flags & FGD_KEY) != 0;
            if( rectState == NOT_SET && !isb && !isf )//ֻ���������ʱ
            {
                rectState = IN_PROCESS; //��ʾ���ڻ�����
                rect = Rect( x, y, 1, 1 );
            }
            if ( (isb || isf) && rectState == SET ) //������alt������shift�����һ����˾��Σ���ʾ���ڻ�ǰ��������
                lblsState = IN_PROCESS;
        }
        break;
    case CV_EVENT_RBUTTONDOWN: // set GC_PR_BGD(GC_PR_FGD) labels
        {
            bool isb = (flags & BGD_KEY) != 0,
                 isf = (flags & FGD_KEY) != 0;
            if ( (isb || isf) && rectState == SET ) //���ڻ����ܵ�ǰ��������
                prLblsState = IN_PROCESS;
        }
        break;
    case CV_EVENT_LBUTTONUP:
        if( rectState == IN_PROCESS )
        {
            rect = Rect( Point(rect.x, rect.y), Point(x,y) );   //���ν���
            rectState = SET;
            setRectInMask();
            assert( bgdPxls.empty() && fgdPxls.empty() && prBgdPxls.empty() && prFgdPxls.empty() );
            showImage();
        }
        if( lblsState == IN_PROCESS )   //�ѻ���ǰ�󾰵�
        {
            setLblsInMask(flags, Point(x,y), false);    //����ǰ����
            lblsState = SET;
            showImage();
        }
        break;
    case CV_EVENT_RBUTTONUP:
        if( prLblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), true); //����������
            prLblsState = SET;
            showImage();
        }
        break;
    case CV_EVENT_MOUSEMOVE:
        if( rectState == IN_PROCESS )
        {
            rect = Rect( Point(rect.x, rect.y), Point(x,y) );
            assert( bgdPxls.empty() && fgdPxls.empty() && prBgdPxls.empty() && prFgdPxls.empty() );
            showImage();    //���ϵ���ʾͼƬ
        }
        else if( lblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), false);
            showImage();
        }
        else if( prLblsState == IN_PROCESS )
        {
            setLblsInMask(flags, Point(x,y), true);
            showImage();
        }
        break;
    }
}

// �Լ�д�ĺ�������������mask�õ�trimap
void GCApplication::GetTrimap(Mat& _mask){

	// Ѱ�������������
	imshow("mask", _mask);
	//imwrite("mask.jpg", _mask);
	vector<vector<Point>> contours; 
	vector<Vec4i> hierarchy; 
	//Mat mask_clone = _mask.clone();
	findContours(_mask, contours, hierarchy, CV_RETR_LIST, CV_CHAIN_APPROX_NONE, Point());
	double contour_area_temp = 0;
	double contour_area_max = 0;
	int max_area_index = 0;

	for (int i = 0; i < contours.size(); i++){
		contour_area_temp = fabs(contourArea(contours[i]));
		if (contour_area_temp > contour_area_max){
			max_area_index = i;
			contour_area_max = contour_area_temp;	
		}
	}
	//// ���������������
 //   Mat maxcontour = Mat::zeros(_mask.size(), CV_8UC1); 
	//for (int i = 0; i < contours[max_area_index].size(); i++){
	//	Point p=Point(contours[max_area_index][i].x,contours[max_area_index][i].y);
	//	maxcontour.at<uchar>(p) = 255;
	//}
	//imshow("�����������", maxcontour);
	

	Mat trimap = Mat::zeros(_mask.size(), CV_8UC1);
	drawContours(trimap, contours, max_area_index, Scalar(255), CV_FILLED);   // ��ʼ��trimap
	imwrite("mask.jpg", trimap); //���������mask
	//ʹ��polygentest�õ���trimap
	//float ratio = 0.05;		//��������������չ����
	//float distance = sqrt((trimap.rows * ratio)*(trimap.rows * ratio) + 
	//	 (trimap.cols * ratio) * (trimap.cols * ratio));		// ��������������չ�ľ���
	//float TempDistance;
	//for (int i = 0; i < trimap.rows; i++){
	//	for (int j = 0; j < trimap.cols; j ++){
	//		TempDistance = fabs(pointPolygonTest(contours[max_area_index], Point2f(j, i), 1));
	//		if (TempDistance < distance){
	//			//cout << TempDistance << endl;
	//			trimap.at<uchar>(i, j) = 128;}
	//	}
	//}

	// ʹ�����͸�ʴ�õ���trimap
	// �����ֵ���û�Ҫ�����о���һ��û��ã��㷨�����Ȳ��ȶ�

	float ratio = 0.05;
	int width_broad = rect.width * ratio;
	int height_broad = rect.height * ratio; 
	Mat trimap_dilate = trimap.clone();
	dilate(trimap, trimap_dilate, Mat(height_broad, width_broad, CV_8U),Point(-1,-1),1);
	erode(trimap, trimap, Mat(height_broad, width_broad, CV_8U),Point(-1,-1), 1);
	// ��ȡ����������
	vector<vector<Point>> outside_contour, inside_contour;
	findContours(trimap_dilate, outside_contour, hierarchy, CV_RETR_LIST, CV_CHAIN_APPROX_NONE, Point());
	findContours(trimap, inside_contour, hierarchy, CV_RETR_LIST, CV_CHAIN_APPROX_NONE, Point());  //ע�⣬�������ɵ�trimapֻ��һ����������

	for (int i = 0; i < trimap.rows; i++){
		for (int j = 0; j < trimap.cols; j++){
			if (pointPolygonTest(outside_contour[0], Point2f(j, i), 0) == 1)
				if (pointPolygonTest(inside_contour[0], Point2f(j, i), 0) == -1)
					trimap.at<uchar>(i, j) = 128;
				else
					trimap.at<uchar>(i, j) = 255;
		}
	}
	vector<int>::iterator iter;
	// ����������
	//for (int i = 0; i < contours[max_area_index].size(); i++){
	//	Point P=Point(contours[max_area_index][i].x,contours[max_area_index][i].y);
	//	trimap.at<uchar>(P) = 255;
	//}


	imshow("trimap", trimap);
	Mat result;
	cvtColor(trimap, result, CV_GRAY2BGR);
	imwrite("trimap.png", trimap);

}
/*�ú�������grabcut�㷨�����ҷ����㷨���е����Ĵ���*/
int GCApplication::nextIter()
{		
	//load the depth image and merge with RGB image
	Mat image_RGBD;
	image_RGBD.create(mask.rows, mask.cols, CV_8UC4);
	vector<Mat> image_per_channel(4), depth;
	split(*image, image_per_channel);

	Mat depth_image = imread("2_depth.jpg", 0);
	split(depth_image, depth);
	for (int i = 0; i < 3; i++)
		image_per_channel[i] = image_per_channel[i] * 0.2;

	image_per_channel.push_back(depth[0]);

	merge(image_per_channel, image_RGBD);

    if( isInitialized ){
        //ʹ��grab�㷨����һ�ε���������2Ϊmask��������maskλ�ǣ������ڲ�������Щ�����Ǳ��������Ѿ�ȷ���Ǳ���������еĵ㣬��maskͬʱҲΪ���
        //������Ƿָ���ǰ��ͼ��
				
        grabCut( image_RGBD, mask, rect, bgdModel, fgdModel, 2 );

		Mat mask_clone = mask.clone();
		for(int i=0;i < mask_clone.rows; i++)
			for(int j = 0;j < mask_clone.cols; j++)
				if (mask_clone.at<uchar>(i, j) % 2 != 0)
					mask_clone.at<uchar>(i, j) = 255;
				else
					mask_clone.at<uchar>(i, j) = 0;
		//GetTrimap(mask_clone);
		// my code
	}
    else
    {
        if( rectState != SET )
            return iterCount;

        if( lblsState == SET || prLblsState == SET )
            grabCut( image_RGBD, mask, rect, bgdModel, fgdModel, 2, GC_INIT_WITH_MASK );
        else
            grabCut( image_RGBD, mask, rect, bgdModel, fgdModel, 2, GC_INIT_WITH_RECT );

        isInitialized = true;
		//my code
		Mat mask_clone = mask.clone();
		for(int i=0;i < mask_clone.rows; i++)
			for(int j = 0;j < mask_clone.cols; j++)
				if (mask_clone.at<uchar>(i, j) % 2 != 0)
					mask_clone.at<uchar>(i, j) = 255;
				else
					mask_clone.at<uchar>(i, j) = 0;
		//GetTrimap(mask_clone);
		// my code

    }
    iterCount++;

    bgdPxls.clear(); fgdPxls.clear();
    prBgdPxls.clear(); prFgdPxls.clear();

    return iterCount;
}

GCApplication gcapp;

static void on_mouse( int event, int x, int y, int flags, void* param )
{
    gcapp.mouseClick( event, x, y, flags, param );
}

int main( int argc, char** argv )
{

    string filename = "2_resize.jpg";
    Mat image = imread( filename, 1 );
    if( image.empty() )
    {
        cout << "\n Durn, couldn't read image filename " << filename << endl;
        return 1;
    }

    help();

    const string winName = "image";
    cvNamedWindow( winName.c_str(), CV_WINDOW_AUTOSIZE );
    cvSetMouseCallback( winName.c_str(), on_mouse, 0 );

    gcapp.setImageAndWinName( image, winName );
    gcapp.showImage();

    for(;;)
    {
        int c = cvWaitKey(0);

        switch( (char) c )
        {
                case '\x1b':
                    cout << "Exiting ..." << endl;
                    goto exit_main;
                case 'r':
                    cout << endl;
                    gcapp.reset();
                    gcapp.showImage();
                    break;
                case 'n':
					double t = (double)getTickCount(); // ����ָ�ʱ��
                    int iterCount = gcapp.getIterCount();
                    cout << "<" << iterCount << "... ";
                    int newIterCount = gcapp.nextIter();
                    if( newIterCount > iterCount )
                    {
                        gcapp.showImage();
                        cout << iterCount << ">" << endl;
                    }
                    else
                        cout << "rect must be determined>" << endl;
					
					t = (double)getTickCount() - t;
					cout << "��ʱ�����룩: " << t / ((double)getTickFrequency()) << endl;
                    break;

					
        }
    }

exit_main:

	cvWaitKey(0);
    destroyAllWindows();
    return 0;
}
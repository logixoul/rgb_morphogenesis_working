#include "precompiled.h"
#if 1
#include "util.h"
#include "stuff.h"
#include "shade.h"
#include "gpgpu.h"
#include "gpuBlur2_4.h"
#include "cfg1.h"
#include "sw.h"
#include "my_console.h"
#include "hdrwrite.h"
#include <float.h>
#include "simplexnoise.h"
#include "mainfunc_impl.h"
#include "colorspaces.h"
#include "easyfft.h"

int wsx=800, wsy = 800 * (800.0f / 1280.0f);
int scale = 2;
int sx = wsx / scale;
int sy = wsy / scale;
bool mouseDown_[3];
bool keys[256];
gl::Texture::Format gtexfmt;
float noiseTimeDim = 0.0f;
float heightmapTimeDim = 0.0f;
const int MAX_AGE = 100;
gl::Texture texToDraw;
bool texOverride = false;

Array2D<float> img(sx, sy);

float mouseX, mouseY;
bool pause;
bool keys2[256];


void updateConfig() {
}

struct SApp : AppBasic {
	Rectf area;
		
	void setup()
	{
		//keys2['0']=keys2['1']=keys2['2']=keys2['3']=true;
		//_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);

		_controlfp(_DN_FLUSH, _MCW_DN);

		area = Rectf(0, 0, (float)sx-1, (float)sy-1).inflated(Vec2f::zero());

		glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);
		glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
		glClampColor(GL_CLAMP_VERTEX_COLOR, GL_FALSE);
		gtexfmt.setInternalFormat(hdrFormat);
		setWindowSize(wsx, wsy);

		glEnable(GL_POINT_SMOOTH);

		Vec2f center = Vec2f(img.Size()) / 2.0f;
		forxy(img) {
			img(p) = p.distance(center) < sy / 3 ? 1 : 0;
		}
	}
	void keyDown(KeyEvent e)
	{
		keys[e.getChar()] = true;
		if(e.isControlDown()&&e.getCode()!=KeyEvent::KEY_LCTRL)
		{
			keys2[e.getChar()] = !keys2[e.getChar()];
			return;
		}
		if(keys['r'])
		{
		}
		if(keys['p'] || keys['2'])
		{
			pause = !pause;
		}
	}
	void keyUp(KeyEvent e)
	{
		keys[e.getChar()] = false;
	}
	
	void mouseDown(MouseEvent e)
	{
		mouseDown_[e.isLeft() ? 0 : e.isMiddle() ? 1 : 2] = true;
	}
	void mouseUp(MouseEvent e)
	{
		mouseDown_[e.isLeft() ? 0 : e.isMiddle() ? 1 : 2] = false;
	}
	Vec2f direction;
	Vec2f lastm;
	void mouseDrag(MouseEvent e)
	{
		mm();
	}
	void mouseMove(MouseEvent e)
	{
		mm();
	}
	void mm()
	{
		direction = getMousePos() - lastm;
		lastm = getMousePos();
	}
	Vec2f reflect(Vec2f const & I, Vec2f const & N)
	{
		return I - N * N.dot(I) * 2.0f;
	}
	float noiseProgressSpeed;
	
	void draw()
	{
		::texOverride = false;

		my_console::beginFrame();
		sw::beginFrame();
		static bool first = true;
		first = false;

		wsx = getWindowSize().x;
		wsy = getWindowSize().y;

		mouseX = getMousePos().x / (float)wsx;
		mouseY = getMousePos().y / (float)wsy;
		/*noiseProgressSpeed=cfg1::getOpt("noiseProgressSpeed", .00008f,
			[&]() { return keys['s']; },
			[&]() { return expRange(mouseY, 0.01f, 100.0f); });*/
		
		gl::clear(Color(0, 0, 0));

		updateIt();
		
		renderIt();

		/*Sleep(50);*/my_console::clr();
		sw::endFrame();
		cfg1::print();
		my_console::endFrame();

		if(pause)
			Sleep(50);
		//Sleep(2000);
	}
	void updateIt() {
		if(!pause) {
			img = gaussianBlur(img, 3);
			Array2D<Vec2f> curvDirs(img.Size(), Vec2f::zero());
			auto grads = ::get_gradients(img);
			for(int x = 0; x < sx; x++)
			{
				for(int y = 0; y < sy; y++)
				{
					Vec2f p = Vec2f(x,y);
					Vec2f grad = grads(x, y).safeNormalized();
					grad = Vec2f(-grad.y, grad.x);;
					Vec2f grad_a = getBilinear(grads, p+grad).safeNormalized();
					grad_a = -Vec2f(-grad_a.y, grad_a.x);
					Vec2f grad_b = getBilinear(grads, p-grad).safeNormalized();
					grad_b = Vec2f(-grad_b.y, grad_b.x);
					Vec2f dir = grad_a + grad_b;
					curvDirs(x, y) = dir;
				}
			}
			forxy(img) {
				float dot = curvDirs(p).dot(grads(p));
				/*if(dot != 0)
					cout << "dot " << dot << endl;*/
				if(dot > 0)
					img(p) += dot * 5.0;
			}
			float sum = std::accumulate(img.begin(), img.end(), 0.0f);
			float avg = sum / (float)img.area;
			forxy(img)
			{
				float f = img(p);
				f += .5f - avg;
				img(p) = f;
			}
			img = to01(img);
			/*forxy(img) {
				img(p) = lerp(img(p), imgb(p), .1f);
			}*/

			if(mouseDown_[0])
			{
				cout << "down" << endl;
				Vec2f scaledm = Vec2f(mouseX * (float)sx, mouseY * (float)sy);
				Area a(scaledm, scaledm);
				int r = 5;
				a.expand(r, r);
				for(int x = a.x1; x <= a.x2; x++)
				{
					for(int y = a.y1; y <= a.y2; y++)
					{
						Vec2f v = Vec2f(x, y) - scaledm;
						float w = max(0.0f, 1.0f - v.length() / r);
						w = 3 * w * w - 2 * w * w * w;
						w=max(0.0f,w);
						img.wr(x, y) += 1.f * w;
					}
				}
			}
		}
	}
	void renderIt() {
		auto tex = gtex(img);

		auto texb = gpuBlur2_4::run(tex, 6);

		tex = shade2(tex, texb,
			"vec3 f1 = fetch3();"
			"vec3 f2 = fetch3(tex2);"
			"vec3 c = .5 * f1 / f2;"
			"c /= c + vec3(1.0);"
			"_out = c;"
			);

		gl::draw(tex, getWindowBounds());
	}
#endif
};
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	return mainFuncImpl(new SApp());
}


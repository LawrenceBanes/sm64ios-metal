//
//  CheatsViewController.m
//  sm64ios
//
//  Created by Christian Kosman on 9/29/21.
//

#import "CheatsViewController.h"
#import "src/pc/cheats.h"
#import "src/pc/mods/mod_runtime.h"
#import "src/pc/fs/fs.h"
#include <math.h>

#if TARGET_OS_IOS
@interface CoopDXModOptionsViewController : UITableViewController
@property(nonatomic) NSInteger modIndex;
@end

@interface CoopDXModManagerViewController : UITableViewController <UIDocumentPickerDelegate>
@end

@implementation CoopDXModManagerViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"CoopDX Mods";
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Import ZIP"
        style:UIBarButtonItemStylePlain target:self action:@selector(importZip)];
    self.refreshControl = [[UIRefreshControl alloc] init];
    [self.refreshControl addTarget:self action:@selector(refreshMods) forControlEvents:UIControlEventValueChanged];
}
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.tableView reloadData];
}
- (void)refreshMods {
    [self.tableView reloadData];
    [self.refreshControl endRefreshing];
}
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    (void)tableView;
    return 2;
}
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;
    return section == 0 ? @"Lua mods" : @"DynOS packs";
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;
    return section == 1 ? @"DynOS packs are imported and retained, but this engine does not yet decode their model, animation, texture, or behavior assets." : nil;
}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;
    return section == 0 ? mod_runtime_mod_count() : mod_runtime_dynos_pack_count();
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    if (indexPath.section == 1) {
        UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
        const char *name = mod_runtime_dynos_pack_name((int)indexPath.row);
        cell.textLabel.text = name ? [NSString stringWithUTF8String:name] : @"DynOS pack";
        const char *summary = mod_runtime_dynos_pack_summary((int)indexPath.row);
        cell.detailTextLabel.text = summary ? [NSString stringWithUTF8String:summary] : @"Imported; DynOS runtime is unavailable";
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        return cell;
    }
    static NSString *reuse = @"CoopDXModCell";
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:reuse];
    if (!cell) cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:reuse];
    const char *name = mod_runtime_mod_name((int)indexPath.row);
    cell.textLabel.text = name ? [NSString stringWithUTF8String:name] : @"Unknown mod";
    cell.detailTextLabel.text = @"Imported into this app's sandbox";
    UISwitch *enabled = [[UISwitch alloc] init];
    enabled.tag = (NSInteger)indexPath.row;
    enabled.on = mod_runtime_mod_enabled((int)indexPath.row);
    enabled.accessibilityLabel = [NSString stringWithFormat:@"Enable %@", cell.textLabel.text];
    [enabled addTarget:self action:@selector(enabledChanged:) forControlEvents:UIControlEventValueChanged];
    cell.accessoryView = enabled;
    return cell;
}
- (void)enabledChanged:(UISwitch *)sender {
    if (!mod_runtime_request_enabled((int)sender.tag, sender.isOn)) {
        sender.on = !sender.isOn;
        [self showMessage:@"The game could not queue that mod change."];
    }
}
- (UISwipeActionsConfiguration *)tableView:(UITableView *)tableView
    trailingSwipeActionsConfigurationForRowAtIndexPath:(NSIndexPath *)indexPath {
    (void)tableView;
    if (indexPath.section != 0) return nil;
    UIContextualAction *reload = [UIContextualAction contextualActionWithStyle:UIContextualActionStyleNormal
        title:@"Reload" handler:^(UIContextualAction *action, UIView *sourceView, void (^completion)(BOOL)) {
            (void)action; (void)sourceView;
            completion(mod_runtime_request_reload((int)indexPath.row));
        }];
    return [UISwipeActionsConfiguration configurationWithActions:@[reload]];
}
- (void)importZip {
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initWithDocumentTypes:@[@"public.zip-archive"] inMode:UIDocumentPickerModeImport];
    picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    NSURL *source = urls.firstObject;
    if (!source) return;
    BOOL scoped = [source startAccessingSecurityScopedResource];
    const char *modsPathC = fs_get_write_path("mods");
    NSString *modsPath = [NSString stringWithUTF8String:modsPathC];
    NSError *error = nil;
    [[NSFileManager defaultManager] createDirectoryAtPath:modsPath withIntermediateDirectories:YES attributes:nil error:&error];
    NSString *incomingPath = [modsPath stringByAppendingPathComponent:
        [NSString stringWithFormat:@".incoming-%@.zip", NSUUID.UUID.UUIDString]];
    NSURL *incomingURL = [NSURL fileURLWithPath:incomingPath];
    BOOL copied = !error && [[NSFileManager defaultManager] copyItemAtURL:source toURL:incomingURL error:&error];
    if (scoped) [source stopAccessingSecurityScopedResource];
    if (!copied || !mod_runtime_request_import_zip(incomingPath.UTF8String)) {
        [[NSFileManager defaultManager] removeItemAtURL:incomingURL error:nil];
        [self showMessage:error.localizedDescription ?: @"Could not queue this ZIP for import."];
        return;
    }
    [self showMessage:@"Import queued. The game will validate and extract the package on its next update; unsupported DynOS assets will be listed as inactive."];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self.tableView reloadData];
    });
}
- (void)showMessage:(NSString *)message {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"CoopDX Mods"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if (indexPath.section != 0) return;
    CoopDXModOptionsViewController *options = [[CoopDXModOptionsViewController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    options.modIndex = indexPath.row;
    options.title = [NSString stringWithUTF8String:mod_runtime_mod_name((int)indexPath.row) ?: "Mod options"];
    [self.navigationController pushViewController:options animated:YES];
}
@end

@implementation CoopDXModOptionsViewController
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.tableView reloadData];
}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView; (void)section;
    return mod_runtime_menu_item_count((int)self.modIndex);
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    (void)tableView;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    ModRuntimeMenuItem item;
    if (!mod_runtime_menu_get_item((int)self.modIndex, (int)indexPath.row, &item)) {
        cell.textLabel.text = @"Unavailable";
        return cell;
    }
    cell.textLabel.text = [NSString stringWithUTF8String:item.label];
    if (item.type == MOD_RUNTIME_MENU_CHECKBOX) {
        UISwitch *toggle = [[UISwitch alloc] init];
        toggle.tag = indexPath.row;
        toggle.on = item.value != 0;
        [toggle addTarget:self action:@selector(menuSwitchChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else if (item.type == MOD_RUNTIME_MENU_SLIDER) {
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%lld (%lld–%lld)",
            (long long)item.value, (long long)item.minimum, (long long)item.maximum];
        UISlider *slider = [[UISlider alloc] initWithFrame:CGRectMake(0, 0, 150, 32)];
        slider.tag = indexPath.row;
        slider.minimumValue = (float)item.minimum;
        slider.maximumValue = (float)item.maximum;
        slider.value = (float)item.value;
        slider.continuous = NO;
        [slider addTarget:self action:@selector(menuSliderChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = slider;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    } else {
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    }
    return cell;
}
- (void)menuSwitchChanged:(UISwitch *)sender {
    if (!mod_runtime_request_menu_set_value((int)self.modIndex, (int)sender.tag, sender.isOn ? 1 : 0))
        sender.on = !sender.isOn;
}
- (void)menuSliderChanged:(UISlider *)sender {
    int64_t value = (int64_t)llroundf(sender.value);
    if (!mod_runtime_request_menu_set_value((int)self.modIndex, (int)sender.tag, value))
        [self.tableView reloadRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:sender.tag inSection:0]] withRowAnimation:UITableViewRowAnimationNone];
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    ModRuntimeMenuItem item;
    if (mod_runtime_menu_get_item((int)self.modIndex, (int)indexPath.row, &item) &&
        item.type == MOD_RUNTIME_MENU_BUTTON &&
        !mod_runtime_request_menu_set_value((int)self.modIndex, (int)indexPath.row, 0))
        [self.tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
}
@end
#endif

@implementation CheatsViewController

- (void)viewDidLoad {
    [super viewDidLoad];
#if TARGET_OS_IOS
    UIView *header = [[UIView alloc] initWithFrame:CGRectMake(0, 0, self.tableView.bounds.size.width, 116)];
    UIStackView *stack = [[UIStackView alloc] initWithFrame:header.bounds];
    stack.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.distribution = UIStackViewDistributionFillEqually;
    stack.spacing = 4;
    UIButton *playground = [UIButton buttonWithType:UIButtonTypeSystem];
    [playground setTitle:@"Open cheat playground" forState:UIControlStateNormal];
    [playground addTarget:self action:@selector(showPlayground) forControlEvents:UIControlEventTouchUpInside];
    UIButton *mods = [UIButton buttonWithType:UIButtonTypeSystem];
    [mods setTitle:@"Manage CoopDX mods" forState:UIControlStateNormal];
    [mods addTarget:self action:@selector(showModManager) forControlEvents:UIControlEventTouchUpInside];
    [stack addArrangedSubview:playground];
    [stack addArrangedSubview:mods];
    [header addSubview:stack];
    self.tableView.tableHeaderView = header;
#endif
}

#if TARGET_OS_IOS
- (void)showPlayground {
    [self presentViewController:ios_cheats_playground_controller() animated:YES completion:nil];
}
- (void)showModManager {
    CoopDXModManagerViewController *manager = [[CoopDXModManagerViewController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    [self.navigationController pushViewController:manager animated:YES];
}

#endif
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.enableCheatsSwitch setOn:Cheats.EnableCheats];
    [self.moonJumpSwitch setOn:Cheats.MoonJump];
    [self.godModeSwitch setOn:Cheats.GodMode];
    [self.infiniteLivesSwitch setOn:Cheats.InfiniteLives];
    [self.superSpeedSwitch setOn:Cheats.SuperSpeed];
    [self.responsiveSwitch setOn:Cheats.Responsive];
    [self.exitAnywhereSwitch setOn:Cheats.ExitAnywhere];
    [self.hugeMarioSwitch setOn:Cheats.HugeMario];
    [self.tinyMarioSwitch setOn:Cheats.TinyMario];
    
#if TARGET_OS_TV
    self.enableCheatsSwitch.callback = ^() {
        [self EnableCheatsChanged:nil];
    };
    self.moonJumpSwitch.callback = ^() {
        [self MoonJumpChanged:nil];
    };
    self.godModeSwitch.callback = ^() {
        [self GodModeChanged:nil];
    };
    self.infiniteLivesSwitch.callback = ^() {
        [self InfiniteLivesChanged:nil];
    };
    self.superSpeedSwitch.callback = ^() {
        [self SuperSpeedChanged:nil];
    };
    self.responsiveSwitch.callback = ^() {
        [self ResponsiveChanged:nil];
    };
    self.exitAnywhereSwitch.callback = ^() {
        [self ExitAnywhereChanged:nil];
    };
    self.hugeMarioSwitch.callback = ^() {
        [self HugeMarioChanged:nil];
    };
    self.tinyMarioSwitch.callback = ^() {
        [self TinyMarioChanged:nil];
    };
#endif
}

- (IBAction)EnableCheatsChanged:(id)sender {
    Cheats.EnableCheats = [self.enableCheatsSwitch isOn];
}

- (IBAction)MoonJumpChanged:(id)sender {
    Cheats.MoonJump = [self.moonJumpSwitch isOn];
}

- (IBAction)GodModeChanged:(id)sender {
    Cheats.GodMode = [self.godModeSwitch isOn];
}

- (IBAction)InfiniteLivesChanged:(id)sender {
    Cheats.InfiniteLives = [self.infiniteLivesSwitch isOn];
}

- (IBAction)SuperSpeedChanged:(id)sender {
    Cheats.SuperSpeed = [self.superSpeedSwitch isOn];
}

- (IBAction)ResponsiveChanged:(id)sender {
    Cheats.Responsive = [self.responsiveSwitch isOn];
}

- (IBAction)ExitAnywhereChanged:(id)sender {
    Cheats.ExitAnywhere = [self.exitAnywhereSwitch isOn];
}

- (IBAction)HugeMarioChanged:(id)sender {
    Cheats.HugeMario = [self.hugeMarioSwitch isOn];
}

- (IBAction)TinyMarioChanged:(id)sender {
    Cheats.TinyMario = [self.tinyMarioSwitch isOn];
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    [self.tableView deselectRowAtIndexPath:indexPath animated:true];
}

@end

#if TARGET_OS_IOS
@interface SM64PlaygroundViewController : UITableViewController
@end

@implementation SM64PlaygroundViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Cheats";
    self.tableView.rowHeight = 64;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
}
- (void)close { [self dismissViewControllerAnimated:YES completion:nil]; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return 5; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return @"Flight: stick steers, A rises, Z descends. Release the stick to coast. Bowser prototype uses native idle, walk and airborne animations, slower ground movement and Mario collision. Flight and Bowser suspend during warps, death, swimming and scripted actions. SuperSlide: running Z+B slide kick or B during a ground pound. A combos out; B rolls out on ground or drops from a wall. Flight, Bowser and an active hat projectile take priority. Settings last for this session.";
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    unsigned settings = cheats_playground_get();
    if (path.row == 0) {
        cell.textLabel.text = @"Mario’s hat";
        UISegmentedControl *hat = [[UISegmentedControl alloc] initWithItems:@[@"Normal", @"ON", @"OFF"]];
        hat.selectedSegmentIndex = settings & CHEAT_HAT_OFF ? 2 : settings & CHEAT_HAT_ON ? 1 : 0;
        hat.accessibilityLabel = @"Mario’s hat";
        [hat addTarget:self action:@selector(hatChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = hat;
    } else if (path.row == 4) {
        cell.textLabel.text = [NSString stringWithFormat:@"Slide cap %u", cheats_superslide_speed()];
        UISlider *speed = [[UISlider alloc] initWithFrame:CGRectMake(0, 0, 160, 44)];
        speed.minimumValue = 30;
        speed.maximumValue = 300;
        speed.value = cheats_superslide_speed();
        speed.accessibilityLabel = @"SuperSlide speed cap";
        speed.accessibilityValue = [NSString stringWithFormat:@"%u", cheats_superslide_speed()];
        [speed addTarget:self action:@selector(slideSpeedChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = speed;
    } else {
        cell.textLabel.text = path.row == 1 ? @"Superman flight" : path.row == 2 ? @"Bowser prototype" : @"SuperSlide";
        UISwitch *toggle = [[UISwitch alloc] init];
        toggle.tag = path.row == 1 ? CHEAT_FLIGHT : path.row == 2 ? CHEAT_BOWSER : CHEAT_SUPERSLIDE;
        toggle.on = (settings & toggle.tag) != 0;
        toggle.accessibilityLabel = cell.textLabel.text;
        [toggle addTarget:self action:@selector(toggleChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
    }
    return cell;
}
- (void)slideSpeedChanged:(UISlider *)sender {
    cheats_set_superslide_speed((unsigned)lroundf(sender.value));
    sender.accessibilityValue = [NSString stringWithFormat:@"%u", cheats_superslide_speed()];
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForRow:4 inSection:0]];
    cell.textLabel.text = [NSString stringWithFormat:@"Slide cap %u", cheats_superslide_speed()];
}
- (void)hatChanged:(UISegmentedControl *)sender {
    unsigned value = sender.selectedSegmentIndex == 1 ? CHEAT_HAT_ON
                   : sender.selectedSegmentIndex == 2 ? CHEAT_HAT_OFF : 0;
    cheats_playground_set(CHEAT_HAT_ON | CHEAT_HAT_OFF, value);
}
- (void)toggleChanged:(UISwitch *)sender {
    cheats_playground_set((unsigned)sender.tag, sender.isOn ? (unsigned)sender.tag : 0);
}
@end

UIViewController *ios_cheats_playground_controller(void) {
    return [[UINavigationController alloc] initWithRootViewController:
        [[SM64PlaygroundViewController alloc] initWithStyle:UITableViewStyleInsetGrouped]];
}

#endif
